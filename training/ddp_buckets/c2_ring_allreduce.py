"""Concept 2: ring all-reduce by hand (reduce-scatter + all-gather) with send/recv.

Run:
    torchrun --nproc_per_node=4 --master_addr=127.0.0.1 --master_port=29500 c2_ring_allreduce.py
    torchrun --nproc_per_node=8 --master_addr=127.0.0.1 --master_port=29500 c2_ring_allreduce.py --numel 8000000

Checks the result against dist.all_reduce and counts bytes each rank sends.
Expected: bytes sent per rank = 2 * (N-1)/N * S, where S = tensor size in bytes.
"""  # noqa: E501

from __future__ import annotations

import argparse
import time

import torch
import torch.distributed as dist

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger


def ring_all_reduce(t: torch.Tensor) -> int:
    """In-place SUM all-reduce over a ring. Returns bytes this rank sent."""
    rank, n = dist.get_rank(), dist.get_world_size()
    right, left = (rank + 1) % n, (rank - 1) % n
    chunks = list(t.chunk(n))  # views into t; numel must divide evenly by n
    recv_buf = torch.empty_like(chunks[0])
    sent = 0

    # Phase 1: reduce-scatter. After N-1 steps, rank r owns the full sum
    # of chunk (r+1) % N.
    for step in range(n - 1):
        send_idx = (rank - step) % n
        recv_idx = (rank - step - 1) % n
        req = dist.isend(chunks[send_idx], dst=right)  # async send so we don't deadlock
        dist.recv(recv_buf, src=left)
        if req is None:
            msg = "isend did not return a work handle"
            raise RuntimeError(msg)
        req.wait()
        chunks[recv_idx].add_(recv_buf)
        sent += chunks[send_idx].numel() * t.element_size()

    # Phase 2: all-gather. Pass the finished chunks around the ring, overwriting.
    for step in range(n - 1):
        send_idx = (rank - step + 1) % n
        recv_idx = (rank - step) % n
        req = dist.isend(chunks[send_idx], dst=right)
        dist.recv(recv_buf, src=left)
        if req is None:
            msg = "isend did not return a work handle"
            raise RuntimeError(msg)
        req.wait()
        chunks[recv_idx].copy_(recv_buf)
        sent += chunks[send_idx].numel() * t.element_size()

    return sent


def main() -> None:
    """Demonstrate ring all-reduce by hand and compare to dist.all_reduce."""
    p = argparse.ArgumentParser()
    p.add_argument("--numel", type=int, default=4_000_000)  # 16 MB of FP32
    args = p.parse_args()

    dist.init_process_group("gloo")
    rank, n = dist.get_rank(), dist.get_world_size()
    numel = args.numel - args.numel % n

    torch.manual_seed(rank)
    data = torch.randn(numel)
    mine, ref = data.clone(), data.clone()

    dist.barrier()
    t0 = time.perf_counter()
    sent = ring_all_reduce(mine)
    t_ring = time.perf_counter() - t0

    dist.barrier()
    t0 = time.perf_counter()
    dist.all_reduce(ref)  # library version
    t_lib = time.perf_counter() - t0

    s_bytes = numel * 4
    if rank == 0:
        logger.info("N=%s, S=%.1f MB", n, s_bytes / 1e6)
        logger.info("matches dist.all_reduce: %s", torch.allclose(mine, ref, atol=1e-4))
        logger.info("bytes sent by rank 0 : %.2f MB", sent / 1e6)
        logger.info("formula 2(N-1)/N * S : %.2f MB", 2 * (n - 1) / n * s_bytes / 1e6)
        logger.info(
            "time  hand-ring %.1f ms | dist.all_reduce %.1f ms",
            t_ring * 1e3,
            t_lib * 1e3,
        )

    dist.destroy_process_group()


if __name__ == "__main__":
    main()

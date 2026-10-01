"""Concept 1: what DDP does during backward — broadcast, buckets, overlap, no_sync.

Run (CPU / gloo, MacBook OK):
    torchrun --nproc_per_node=2 --master_addr=127.0.0.1 --master_port=29500 c1_ddp_buckets.py
    torchrun --nproc_per_node=2 --master_addr=127.0.0.1 --master_port=29500 c1_ddp_buckets.py --bucket-cap-mb 1
    torchrun --nproc_per_node=2 --master_addr=127.0.0.1 --master_port=29500 c1_ddp_buckets.py --grad-accum 4
    torchrun --nproc_per_node=2 --master_addr=127.0.0.1 --master_port=29500 c1_ddp_buckets.py --grad-accum 4 --no-sync
"""  # noqa: E501

from __future__ import annotations

import argparse
import contextlib
import time

import torch
import torch.distributed as dist
from torch import nn
from torch.distributed.algorithms.ddp_comm_hooks import default_hooks
from torch.nn.parallel import DistributedDataParallel

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger


def make_model() -> nn.Sequential:
    """Make a simple model with 8 linear layers and ReLU activations."""
    # 8 x Linear(1024, 1024): each weight ~4 MB in FP32 -> ~33 MB of gradients in total
    layers: list[nn.Module] = []
    for _ in range(8):
        layers += [nn.Linear(1024, 1024), nn.ReLU()]
    return nn.Sequential(*layers)


def _run_training_steps(ddp, opt, grad_accum, no_sync, events, t0):  # noqa: PLR0913
    """Run two optimizer steps and return the final step's backward spans."""
    for _ in range(2):
        events.clear()
        opt.zero_grad(set_to_none=True)
        t0[0] = time.perf_counter()
        bwd_spans = []
        for micro in range(grad_accum):
            last = micro == grad_accum - 1
            ctx = ddp.no_sync() if (no_sync and not last) else contextlib.nullcontext()
            with ctx:
                x = torch.randn(64, 1024)
                loss = ddp(x).pow(2).mean()
                b0 = time.perf_counter() - t0[0]
                loss.backward()  # hooks fire INSIDE this call
                bwd_spans.append((b0, time.perf_counter() - t0[0]))
        opt.step()
    return bwd_spans


def main() -> None:
    """Demonstrate DDP initialization, gradient buckets, and synchronization."""
    p = argparse.ArgumentParser()
    p.add_argument("--bucket-cap-mb", type=float, default=25.0)
    p.add_argument("--grad-accum", type=int, default=1)
    p.add_argument("--no-sync", action="store_true")
    args = p.parse_args()

    dist.init_process_group("gloo")
    rank, world = dist.get_rank(), dist.get_world_size()

    # (A) Broadcast at construction: give every rank a DIFFERENT init on purpose.
    torch.manual_seed(1234 + rank)
    model = make_model()
    before = next(model.parameters())[0, 0].item()
    ddp = DistributedDataParallel(model, bucket_cap_mb=args.bucket_cap_mb)
    after = next(model.parameters())[0, 0].item()
    vals = [None] * world
    dist.all_gather_object(vals, (before, after))
    if rank == 0:
        logger.info("[A] (before DDP, after DDP) per rank: %s", vals)
        logger.info("    -> after DDP() every rank holds rank 0's weights\n")

    # (B) Comm hook: called once per bucket, at the moment that bucket is ready.
    name_of = {id(p): n for n, p in ddp.module.named_parameters()}
    events: list[tuple[float, int, float, list[str]]] = []
    t0 = [0.0]

    def logging_hook(state, bucket):
        names = [name_of[id(p)] for p in bucket.parameters()]
        size_mb = bucket.buffer().numel() * bucket.buffer().element_size() / 1e6
        events.append((time.perf_counter() - t0[0], bucket.index(), size_mb, names))
        return default_hooks.allreduce_hook(
            state, bucket
        )  # normal all-reduce (averaged)

    ddp.register_comm_hook(state=None, hook=logging_hook)
    opt = torch.optim.SGD(ddp.parameters(), lr=0.01)

    # Step 0 lets DDP rebuild buckets from the real gradient order; we report step 1.
    bwd_spans = _run_training_steps(ddp, opt, args.grad_accum, args.no_sync, events, t0)

    if rank == 0:
        logger.info(
            "[B] bucket_cap_mb=%s, grad_accum=%s, no_sync=%s",
            args.bucket_cap_mb,
            args.grad_accum,
            args.no_sync,
        )
        for i, (b0, b1) in enumerate(bwd_spans):
            logger.info(
                "    micro %s: backward ran %7.1f -> %7.1f ms",
                i,
                b0 * 1e3,
                b1 * 1e3,
            )
        for t, idx, mb, names in events:
            logger.info(
                "    t=%7.1f ms  bucket %s  %5.1f MB  %s .. %s",
                t * 1e3,
                idx,
                mb,
                names[0],
                names[-1],
            )
        logger.info("    all-reduce calls this optimizer step: %s", len(events))
        logger.info(
            "    -> bucket 0 holds the LAST layers (grads are ready in reverse order)"
        )
        logger.info(
            "    -> hook times sit inside a backward span = comm overlaps compute\n"
        )

    # (C) After the step every rank has the same averaged grads -> same weights.
    checksum = torch.tensor([sum(p.detach().sum().item() for p in ddp.parameters())])
    sums = [torch.zeros(1) for _ in range(world)]
    dist.all_gather(sums, checksum)
    if rank == 0:
        logger.info(
            "[C] weight checksum per rank: %s",
            [round(s.item(), 6) for s in sums],
        )

    dist.destroy_process_group()


if __name__ == "__main__":
    main()

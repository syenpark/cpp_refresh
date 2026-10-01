"""Concept 4: ZeRO-3 / FSDP by hand for one weight matrix.

Checked against plain DDP math.

Run:
    torchrun --nproc_per_node=2 c4_fsdp_by_hand.py
    torchrun --nproc_per_node=4 c4_fsdp_by_hand.py

Each rank keeps only 1/N of the weight between steps.
  forward : all-gather shards -> full weight (temporary)
    backward: full grad on each rank -> reduce-scatter -> each rank keeps 1/N
                        of the averaged grad
  step    : update only the local shard; free the full weight
Gloo has no reduce_scatter, so it is emulated as all_reduce + slice (NCCL
would use dist.reduce_scatter_tensor). Real FSDP also frees the full weight
after forward and all-gathers it AGAIN in backward -> that is where the 3rd
(N-1)/N*S of traffic comes from.
"""

from __future__ import annotations

import torch
import torch.distributed as dist

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger

D_OUT, D_IN, LR, STEPS = 32, 64, 0.1, 5


def main() -> None:
    """Compare a manually sharded FSDP update with plain DDP math."""
    dist.init_process_group("gloo")
    rank, n = dist.get_rank(), dist.get_world_size()

    g = torch.Generator().manual_seed(0)
    full_init = torch.randn(D_OUT, D_IN, generator=g)  # same on every rank
    numel = full_init.numel()
    if numel % n != 0:
        msg = f"Number of elements {numel} is not divisible by world size {n}"
        logger.error(msg)
        raise ValueError(msg)
    s = numel // n

    my_shard = full_init.flatten()[rank * s : (rank + 1) * s].clone()  # FSDP state: 1/N
    ref_w = full_init.clone()  # DDP state: full copy

    for step in range(STEPS):
        x = torch.randn(
            8, D_IN, generator=torch.Generator().manual_seed(100 * step + rank)
        )

        # ---- FSDP path ----
        shards = [torch.empty(s) for _ in range(n)]
        dist.all_gather(shards, my_shard)  # 1) all-gather params
        w = torch.cat(shards).view(D_OUT, D_IN).requires_grad_()
        (x @ w.t()).pow(2).mean().backward()
        weight_grad = w.grad
        if weight_grad is None:
            msg = "FSDP-path weight gradient was not computed"
            raise RuntimeError(msg)
        grad = weight_grad.flatten()
        dist.all_reduce(grad)  # 2) reduce-scatter (emulated)
        grad /= n
        my_grad = grad[rank * s : (rank + 1) * s]
        with torch.no_grad():
            my_shard -= LR * my_grad  # 3) update only my shard
        del w, shards, grad  # 4) free the full weight

        # ---- DDP reference path ----
        rw = ref_w.clone().requires_grad_()
        (x @ rw.t()).pow(2).mean().backward()
        ref_grad = rw.grad
        if ref_grad is None:
            msg = "DDP-reference weight gradient was not computed"
            raise RuntimeError(msg)
        dist.all_reduce(ref_grad)
        ref_grad /= n
        ref_w = (rw - LR * ref_grad).detach()

    shards = [torch.empty(s) for _ in range(n)]
    dist.all_gather(shards, my_shard)
    fsdp_w = torch.cat(shards).view(D_OUT, D_IN)
    if rank == 0:
        logger.info(
            "N=%d: FSDP result == DDP result: %s",
            n,
            torch.allclose(fsdp_w, ref_w, atol=1e-6),
        )
        logger.info(
            "persistent floats per rank: FSDP %d  vs  DDP %d  (%dx less)", s, numel, n
        )

    dist.destroy_process_group()


if __name__ == "__main__":
    main()

"""Concept 5: why distributed jobs hang, and how a timeout turns a hang into an error.

Run (skip mode takes ~30 s):
    torchrun --nproc_per_node=2 --master_addr=127.0.0.1 --master_port=29500 c5_hang_demo.py --mode ok
    torchrun --nproc_per_node=2 --master_addr=127.0.0.1 --master_port=29500 c5_hang_demo.py --mode skip   # rank 1 skips a collective
    torchrun --nproc_per_node=2 --master_addr=127.0.0.1 --master_port=29500 c5_hang_demo.py --mode die    # rank 1 crashes (like an OOM)

In "die" mode torchrun itself notices the dead worker and SIGTERMs the others, so rank 0
may be killed before it prints. That is the elastic agent doing its job (see --max-restarts).

Watch what rank 0 prints in each mode. Same idea with NCCL on GPUs:
    NCCL_DEBUG=INFO                 -> which transport / NIC NCCL picked
    TORCH_DISTRIBUTED_DEBUG=DETAIL  -> checks that all ranks call the same collective
    init_process_group(timeout=...) -> fail fast instead of hanging forever
"""  # noqa: E501

from __future__ import annotations

import argparse
import sys
import time
from datetime import timedelta

import torch
import torch.distributed as dist

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger


def main() -> None:
    """Demonstrate a hang and how to turn it into an error with a timeout."""
    p = argparse.ArgumentParser()
    p.add_argument("--mode", choices=["ok", "skip", "die"], default="ok")
    args = p.parse_args()

    dist.init_process_group("gloo", timeout=timedelta(seconds=10))
    rank = dist.get_rank()
    t = torch.ones(4)

    for step in range(3):
        if rank == 1 and step == 1:
            if args.mode == "skip":
                # e.g. `if loss_is_nan: continue` on ONE rank only -> others wait
                # forever
                logger.info("rank %s: skipping all_reduce at step 1 and idling", rank)
                time.sleep(30)
                break
            if args.mode == "die":
                logger.info("rank %s: crashing at step 1", rank)
                sys.exit(1)
        start = time.perf_counter()
        try:
            dist.all_reduce(t)
            logger.info("rank %s: step %s all_reduce ok", rank, step)
        except RuntimeError as e:
            waited = time.perf_counter() - start
            logger.info(
                "rank %s: step %s FAILED after %.1fs -> %.120s",
                rank,
                step,
                waited,
                str(e),
            )
            break

    dist.destroy_process_group()


if __name__ == "__main__":
    main()

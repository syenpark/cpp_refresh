"""DDP Training Example."""

from __future__ import annotations

import os
import time

import torch
import torch.distributed as dist

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger


def main():
    """Main function for DDP training example."""
    dist.init_process_group(backend="gloo")

    rank = dist.get_rank()
    world_size = dist.get_world_size()

    started_at = time.perf_counter()
    logger.info("pid=%s rank=%s, world_size=%s", os.getpid(), rank, world_size)

    tensor = torch.tensor([rank], dtype=torch.float32)

    logger.info("rank=%s entering barrier", rank)
    dist.barrier()
    logger.info(
        "rank=%s left barrier after %.3fs", rank, time.perf_counter() - started_at
    )

    dist.all_reduce(tensor, op=dist.ReduceOp.SUM)

    logger.info("rank=%s, all_reduce_result=%s", rank, tensor.item())

    dist.destroy_process_group()


if __name__ == "__main__":
    main()

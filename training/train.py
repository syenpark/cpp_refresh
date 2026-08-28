"""DDP Training Example."""

from __future__ import annotations

import os
import time

import torch
import torch.distributed as dist  # Distributed process communication API

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger


def main():
    """Main function for DDP training example."""
    # Join/create communication within the distributed process group
    dist.init_process_group(backend="gloo")

    # This process's global rank
    rank = dist.get_rank()
    # Total number of processes in the group
    world_size = dist.get_world_size()

    started_at = time.perf_counter()
    logger.info("pid=%s rank=%s, world_size=%s", os.getpid(), rank, world_size)

    tensor = torch.tensor([rank], dtype=torch.float32)

    logger.info("rank=%s entering barrier", rank)
    # Wait here until all ranks reach this point
    dist.barrier()
    logger.info(
        "rank=%s left barrier after %.3fs", rank, time.perf_counter() - started_at
    )

    # Sum tensors across all ranks and give the summed result to every rank
    dist.all_reduce(tensor, op=dist.ReduceOp.SUM)

    logger.info("rank=%s, all_reduce_result=%s", rank, tensor.item())

    # Clean up distributed communication resources
    dist.destroy_process_group()


if __name__ == "__main__":
    main()

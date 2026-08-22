"""DDP Training Example."""

from __future__ import annotations

import torch
import torch.distributed as dist

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger


def main():
    """Main function for DDP training example."""
    dist.init_process_group(backend="gloo")

    rank = dist.get_rank()
    world_size = dist.get_world_size()

    logger.info("rank=%s, world_size=%s", rank, world_size)

    tensor = torch.tensor([rank], dtype=torch.float32)

    dist.all_reduce(tensor, op=dist.ReduceOp.SUM)

    logger.info("rank=%s, all_reduce_result=%s", rank, tensor.item())

    dist.destroy_process_group()


if __name__ == "__main__":
    main()

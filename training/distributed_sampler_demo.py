"""DistributedSampler demonstration."""

from __future__ import annotations

import torch.distributed as dist
from torch.utils.data import DataLoader
from torch.utils.data import Dataset
from torch.utils.data.distributed import DistributedSampler

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger


class NumberDataset(Dataset):
    """Simple dataset returning index numbers."""

    def __init__(self, size: int) -> None:
        self.data = list(range(size))

    def __len__(self) -> int:
        """Return the number of samples."""
        return len(self.data)

    def __getitem__(self, index: int) -> int:
        """Return the sample at the given index."""
        return self.data[index]


def main() -> None:
    """Run DistributedSampler demo."""
    dist.init_process_group(backend="gloo")

    rank = dist.get_rank()
    world_size = dist.get_world_size()

    dataset = NumberDataset(size=16)

    sampler = DistributedSampler(
        dataset,
        num_replicas=world_size,
        rank=rank,
        shuffle=False,
    )

    loader = DataLoader(
        dataset,
        batch_size=1,
        sampler=sampler,
    )

    samples = [sample.item() for sample in loader]
    logger.info(
        "rank=%s, world_size=%s, samples=%s",
        rank,
        world_size,
        samples,
    )

    dist.destroy_process_group()


if __name__ == "__main__":
    main()

"""DDP DataLoader benchmark for synchronization experiments."""

from __future__ import annotations

import argparse
import os
import time

import torch
import torch.distributed as dist
from torch.nn.parallel import DistributedDataParallel
from torch.utils.data import DataLoader
from torch.utils.data import Dataset
from torch.utils.data.distributed import DistributedSampler

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger


class SyntheticDataset(Dataset):
    """Synthetic dataset with configurable CPU-heavy preprocessing."""

    def __init__(self, size: int, work: int) -> None:
        self.size = size
        self.work = work

    def __len__(self) -> int:
        """Return the number of samples."""
        return self.size

    def __getitem__(self, index: int) -> tuple[torch.Tensor, torch.Tensor]:
        """Generate one CPU-heavy training sample."""
        # Intentionally CPU-heavy preprocessing.
        x = torch.tensor(float(index))

        for _ in range(self.work):
            x = torch.sin(x) + torch.cos(x)

        y = x * 2.0

        return x.unsqueeze(0), y.unsqueeze(0)


def parse_args() -> argparse.Namespace:
    """Parse CLI arguments."""
    parser = argparse.ArgumentParser()

    parser.add_argument("--num-workers", type=int, default=0)
    parser.add_argument("--batch-size", type=int, default=64)
    parser.add_argument("--dataset-size", type=int, default=10_000)
    parser.add_argument("--work", type=int, default=100)
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--before-backward", type=int, default=0)
    parser.add_argument("--no-sampler", action="store_true")
    return parser.parse_args()


def main() -> None:
    """Run the DDP DataLoader benchmark."""
    args = parse_args()

    dist.init_process_group(backend="gloo")

    rank = dist.get_rank()
    world_size = dist.get_world_size()

    torch.manual_seed(42)

    dataset = SyntheticDataset(
        size=args.dataset_size,
        work=args.work,
    )

    sampler: DistributedSampler[SyntheticDataset] | None = (
        None
        if args.no_sampler
        else DistributedSampler(
            dataset,
            num_replicas=world_size,
            rank=rank,
            shuffle=False,
        )
    )

    loader = DataLoader(
        dataset,
        batch_size=args.batch_size,
        num_workers=args.num_workers,
        shuffle=False,
        sampler=sampler,
    )

    model = DistributedDataParallel(torch.nn.Linear(1, 1))

    optimizer = torch.optim.SGD(model.parameters(), lr=0.01)
    loss_fn = torch.nn.MSELoss()

    if rank == 0:
        logger.info(
            "world_size=%s num_workers=%s pid=%s",
            world_size,
            args.num_workers,
            os.getpid(),
        )

    dist.barrier()
    start = time.perf_counter()

    total_samples = 0

    for epoch in range(args.epochs):
        if sampler is not None:
            sampler.set_epoch(epoch)
        for step, (x, y) in enumerate(loader):
            if epoch == 0 and step == 0:
                logger.info(
                    "rank=%s first batch x[:3]=%s",
                    rank,
                    x[:3].flatten().tolist(),
                )
            optimizer.zero_grad()

            prediction = model(x)
            loss = loss_fn(prediction, y)

            loss.backward()
            optimizer.step()

            total_samples += x.size(0)

    dist.barrier()
    logger.info("rank=%s total_samples=%s", rank, total_samples)

    elapsed = time.perf_counter() - start

    local_throughput = total_samples / elapsed

    throughput = torch.tensor(local_throughput)
    dist.reduce(
        throughput,
        dst=0,
        op=dist.ReduceOp.SUM,
    )

    if rank == 0:
        logger.info("elapsed=%.2fs", elapsed)
        logger.info("throughput=%.2f samples/s", throughput.item())

    dist.destroy_process_group()


if __name__ == "__main__":
    main()

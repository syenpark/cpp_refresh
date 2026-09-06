"""DDP straggler benchmark for synchronization experiments."""

from __future__ import annotations

import argparse
import os
import time

import torch
import torch.distributed as dist
from torch.nn.parallel import DistributedDataParallel
from torch.utils.data import DataLoader
from torch.utils.data import Dataset

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger

RANK = 2


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
    parser.add_argument("--before-backward", type=int, default=2)

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

    loader = DataLoader(
        dataset,
        batch_size=args.batch_size,
        num_workers=args.num_workers,
        shuffle=False,
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

    for epoch_idx in range(args.epochs):
        for batch_idx, (x, y) in enumerate(loader):
            optimizer.zero_grad()

            prediction = model(x)
            loss = loss_fn(prediction, y)

            # Inject exactly one artifical starggler.
            if rank == RANK and epoch_idx == 0 and batch_idx == 0:
                time.sleep(0.5)

            t0 = time.perf_counter()
            loss.backward()
            backward_time = time.perf_counter() - t0

            if epoch_idx == 0 and batch_idx == 0:
                logger.info(
                    "rank=%s backward_time=%.4fs",
                    rank,
                    backward_time,
                )
            optimizer.step()

            total_samples += x.size(0)

    dist.barrier()

    elapsed = time.perf_counter() - start

    total_samples_tensor = torch.tensor(float(total_samples))

    dist.reduce(
        total_samples_tensor,
        dst=0,
        op=dist.ReduceOp.SUM,
    )

    if rank == 0:
        global_throughput = total_samples_tensor.item() / elapsed
        logger.info("elapsed=%.2fs", elapsed)
        logger.info("throughput=%.2f samples/s", global_throughput)

    dist.destroy_process_group()


if __name__ == "__main__":
    main()

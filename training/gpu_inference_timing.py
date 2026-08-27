"""GPU inference timing example."""

from __future__ import annotations

import time

import torch

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger

device = torch.device("mps")


def model(x: torch.Tensor) -> torch.Tensor:
    """Simple model for GPU inference timing."""
    return x @ x


def naive_cpu_timer(x: torch.Tensor) -> float:
    """Naive CPU timer for GPU operations."""
    start = time.perf_counter()
    model(x)

    return time.perf_counter() - start


def sync_gpu_timer(x: torch.Tensor) -> float:
    """Synchronized GPU timer for GPU operations."""
    torch.mps.synchronize()
    start = time.perf_counter()
    model(x)
    torch.mps.synchronize()  # Ensure all GPU operations are complete

    return time.perf_counter() - start


def isolated_h2d_vs_inference_timer(x: torch.Tensor) -> tuple[float, float]:
    """Isolated H2D copy vs inference timing."""
    torch.mps.synchronize()
    start_h2d = time.perf_counter()
    x_gpu = x.to(device)
    torch.mps.synchronize()  # Ensure H2D copy is complete if in NVIDIA GPU context
    h2d_time = time.perf_counter() - start_h2d

    # Inference timing
    start_inference = time.perf_counter()
    model(x_gpu)
    torch.mps.synchronize()  # Ensure inference is complete
    inference_time = time.perf_counter() - start_inference

    return h2d_time, inference_time


def main() -> None:
    """Run the GPU inference timing example."""
    x_cpu = torch.randn(4096, 4096)
    x_gpu = x_cpu.to(device)

    # Warm-up
    # The first few GPU operations can include setup/caching effects,
    # so one cold measurement can be misleading.
    for _ in range(10):
        model(x_gpu)
    torch.mps.synchronize()

    naive_time = naive_cpu_timer(x_cpu)
    logger.info("naive: %f", naive_time)

    sync_gpu_timer_time = sync_gpu_timer(x_gpu)
    logger.info("sync_gpu_timer: %f", sync_gpu_timer_time)

    isolated_h2d_time, isolated_inference_time = isolated_h2d_vs_inference_timer(x_cpu)
    logger.info(
        "isolated_h2d_time: %f, isolated_inference_time: %f",
        isolated_h2d_time,
        isolated_inference_time,
    )


if __name__ == "__main__":
    main()

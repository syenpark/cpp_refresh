"""GPU inference profiling example for MPS and CUDA."""

from __future__ import annotations

import argparse
import statistics
import time
from typing import Any

import torch

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger


def model(x: torch.Tensor) -> torch.Tensor:
    """Simple model for GPU inference timing."""
    return x @ x


def cpu_preprocess(x: torch.Tensor) -> torch.Tensor:
    """Artificial CPU preprocessing workload."""
    for _ in range(200):
        x = torch.sin(x) + torch.cos(x)

    return x


def measure_preprocess(x: torch.Tensor) -> tuple[torch.Tensor, float]:
    """Measure CPU preprocessing latency."""
    start = time.perf_counter()

    x = cpu_preprocess(x)

    elapsed = time.perf_counter() - start

    return x, elapsed


def synchronize(device: torch.device) -> None:
    """Wait for pending work on the selected accelerator."""
    if device.type == "cuda":
        torch.cuda.synchronize()
    else:
        torch.mps.synchronize()


def measure_forward(
    x: torch.Tensor, device: torch.device
) -> tuple[torch.Tensor, float]:
    """Measure actual GPU forward latency."""
    synchronize(device)

    start = time.perf_counter()

    with torch.no_grad():
        output = model(x)

    # Wait until GPU really finishes.
    synchronize(device)

    return output, time.perf_counter() - start


def select_device(requested_device: str) -> torch.device:
    """Select an explicitly requested or automatically detected accelerator."""
    if requested_device == "cuda":
        if not torch.cuda.is_available():
            msg = "CUDA was requested but is not available"
            raise RuntimeError(msg)
        return torch.device("cuda")

    if requested_device == "mps":
        if not torch.backends.mps.is_available():
            msg = "MPS was requested but is not available"
            raise RuntimeError(msg)
        return torch.device("mps")

    if torch.cuda.is_available():
        return torch.device("cuda")
    if torch.backends.mps.is_available():
        return torch.device("mps")
    msg = "No CUDA or MPS device is available"
    raise RuntimeError(msg)


def measure_postprocess(x: torch.Tensor) -> tuple[torch.Tensor, float, float]:
    """Measure D2H transfer and CPU postprocessing latency."""
    # D2H
    start = time.perf_counter()
    output_cpu = x.cpu()
    d2h_time = time.perf_counter() - start

    # CPU postprocessing
    start = time.perf_counter()
    result = torch.clamp(output_cpu, min=0.0)
    postprocess_time = time.perf_counter() - start

    return result, d2h_time, postprocess_time


def measure_h2d_transfer(x_processed: torch.Tensor, device: torch.device) -> float:
    """Measure pure host-to-device transfer latency for a fixed processed tensor."""
    start = time.perf_counter()
    _ = x_processed.to(device)
    synchronize(device)
    return time.perf_counter() - start


def profiling_context(device: torch.device) -> tuple[Any, Any]:
    """Return the profiler context and optional profiler object for a device."""
    if device.type == "mps":
        import torch.mps.profiler as mps_profiler  # noqa: PLC0415

        return mps_profiler.profile(), None

    profiler = torch.profiler.profile(
        activities=[
            torch.profiler.ProfilerActivity.CPU,
            torch.profiler.ProfilerActivity.CUDA,
        ],
        record_shapes=True,
    )
    return profiler, profiler


def main() -> None:
    """Run GPU inference profiling experiment."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", choices=("auto", "mps", "cuda"), default="auto")
    args = parser.parse_args()
    device = select_device(args.device)

    x_cpu = torch.randn(4096, 4096)

    # Warm-up GPU
    x_gpu = x_cpu.to(device)

    for _ in range(10):
        _ = model(x_gpu)

    synchronize(device)

    preprocess_times: list[float] = []
    h2d_times = []
    forward_times = []
    d2h_times = []
    postprocess_times = []
    profiler_context, profiler = profiling_context(device)

    # Isolate CPU preprocessing from the pure H2D transfer measurement.
    for _ in range(10):
        x_processed, preprocess_time = measure_preprocess(x_cpu)
        preprocess_times.append(preprocess_time)

    # Measure transfer repeatedly using one fixed processed tensor.
    h2d_times = [measure_h2d_transfer(x_processed, device) for _ in range(100)]

    with profiler_context:
        for _ in range(10):
            # GPU stage uses the fixed processed tensor so transfer cost is not
            # mixed with the heavy CPU preprocessing workload.
            x_gpu = x_processed.to(device)
            output, forward_time = measure_forward(x_gpu, device)
            forward_times.append(forward_time)

            # Postprocessing stage
            _, d2h_time, postprocess_time = measure_postprocess(output)
            d2h_times.append(d2h_time)
            postprocess_times.append(postprocess_time)

    if profiler is not None:
        profiler.export_chrome_trace("gpu_inference_cuda_trace.json")

    logger.info(
        "median preprocess latency: %.4fs",
        statistics.median(preprocess_times),
    )

    logger.info(
        "median H2D latency: %.4fs",
        statistics.median(h2d_times),
    )

    logger.info(
        "median forward+synchronize latency: %.4fs",
        statistics.median(forward_times),
    )

    logger.info(
        "median D2H latency: %.4fs",
        statistics.median(d2h_times),
    )

    logger.info(
        "median postprocess latency: %.4fs",
        statistics.median(postprocess_times),
    )


if __name__ == "__main__":
    main()

"""Concept 3 + 4: back-of-envelope numbers for DDP scaling and FSDP memory. Pure Python.

Run:  python c3_cost_model.py
Edit the numbers at the bottom and re-run; this is the calculator to use in interviews.
"""

from __future__ import annotations

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger

GB = 1e9


def allreduce_ms(
    size_bytes: float, n: int, bw_gbps: float, alpha_us: float = 5.0
) -> float:
    """Ring all-reduce time: bandwidth term + latency term.

    bytes per rank = 2 * (n-1)/n * S
    time          = bytes / bandwidth + 2 * (n-1) * alpha
    """
    if n == 1:
        return 0.0
    bw_term = 2 * (n - 1) / n * size_bytes / (bw_gbps * GB)
    lat_term = 2 * (n - 1) * alpha_us * 1e-6
    return (bw_term + lat_term) * 1e3


def ddp_step(
    fwd_ms: float, bwd_ms: float, opt_ms: float, comm_ms: float, overlap: float = 1.0
):
    """Comm can hide behind backward only. overlap=1.0 means perfect bucketing."""
    hidden = min(comm_ms, bwd_ms * overlap)
    exposed = comm_ms - hidden
    step = fwd_ms + bwd_ms + exposed + opt_ms
    return step, exposed


def scaling_table(  # noqa: PLR0913
    params: float,
    n_list,
    links: dict[str, float],
    fwd=33.0,
    bwd=66.0,
    opt=1.0,
) -> None:
    """Print a table of weak-scaling step times and efficiency for different links."""
    grad_bytes = params * 4  # FP32 grads
    t1 = fwd + bwd + opt
    logger.info(
        "\nModel %.0fM params -> grads %.0f MB; 1-GPU step %.0f ms",
        params / 1e6,
        grad_bytes / 1e6,
        t1,
    )
    logger.info(
        "%-14s%3s%10s%10s%10s%12s",
        "link",
        "N",
        "comm ms",
        "exposed",
        "step ms",
        "efficiency",
    )
    for name, bw in links.items():
        for n in n_list:
            comm = allreduce_ms(grad_bytes, n, bw)
            step, exposed = ddp_step(fwd, bwd, opt, comm)
            eff = t1 / step  # weak scaling: throughput_N / (N * throughput_1)
            logger.info(
                "%-14s%3d%10.1f%10.1f%10.1f%11.0%",
                name,
                n,
                comm,
                exposed,
                step,
                eff,
            )


def memory_per_gpu_gb(params: float, n: int, mode: str) -> float:
    """Model Adam + mixed-precision states.

    2 (bf16 param) + 2 (grad) + 12 (fp32 master, m, v).
    """
    p, g, o = 2 * params, 2 * params, 12 * params
    if mode == "DDP":
        return (p + g + o) / GB
    if mode == "ZeRO-1":  # shard optimizer state
        return (p + g + o / n) / GB
    if mode == "ZeRO-2":  # + shard grads
        return (p + (g + o) / n) / GB
    if mode == "FSDP/ZeRO-3":  # + shard params
        return ((p + g + o) / n) / GB
    raise ValueError(mode)


def comm_per_step(params: float, n: int) -> None:
    """Bytes sent per rank per step for DDP vs FSDP."""
    s = params * 2  # bf16 bytes
    k = (n - 1) / n
    logger.info(
        "\nComm per step, %.0fB params, N=%d (bytes sent per rank):",
        params / 1e9,
        n,
    )
    logger.info(
        "  DDP   all-reduce grads                      = 2*k*S = %6.1f GB",
        2 * k * s / GB,
    )
    logger.info(
        "  FSDP  all-gather(fwd)+all-gather(bwd)+RS    = 3*k*S = %6.1f GB  (1.5x DDP)",
        3 * k * s / GB,
    )


if __name__ == "__main__":
    # Approximate effective bandwidths (GB/s). Replace with your nccl-tests busbw.
    links = {"NVLink": 200.0, "PCIe Gen4": 22.0, "25GbE": 3.0, "10GbE": 1.1}
    scaling_table(params=25e6, n_list=[2, 4, 8], links=links)  # ResNet-50-ish
    scaling_table(params=350e6, n_list=[2, 4, 8], links=links)  # mid-size transformer

    logger.info(
        "\nModel-state memory per GPU (GB), 7B params, N=8, excluding activations:"
    )
    for mode in ["DDP", "ZeRO-1", "ZeRO-2", "FSDP/ZeRO-3"]:
        logger.info("  %-12s %6.1f", mode, memory_per_gpu_gb(7e9, 8, mode))
    comm_per_step(7e9, 8)

"""GPU inference timing example."""

from __future__ import annotations

import time

import torch

from py.utils.custom_logging import SetLogger

logger = SetLogger().logger

device = torch.device("mps")

x = torch.randn(4096, 4096, device=device)

# Warm-up
for _ in range(10):
    y = x @ x

torch.mps.synchronize()

start = time.perf_counter()
y = x @ x
end = time.perf_counter()

logger.info("naive: %f", end - start)

torch.mps.synchronize()

start = time.perf_counter()
y = x @ x
torch.mps.synchronize()
end = time.perf_counter()

logger.info("synchronized: %f", end - start)

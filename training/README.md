# Training Lab

PyTorch distributed-training and GPU timing experiments.

## DDP Smoke Test

Run two local CPU workers with Gloo:

```bash
podman run --rm -it \
  ghcr.io/syenpark/pytorch-ddp:latest \
  torchrun --standalone --nproc-per-node=2 -m training.train
```

Expected:

```text
rank=0, world_size=2
rank=1, world_size=2
all_reduce_result=1.0
```

This validates:

* `torchrun`
* process groups
* `rank`
* `world_size`
* `all_reduce`
* Gloo communication

## GPU Timing on M2

Run:

```bash
python gpu_inference_timing.py
```

Example result:

```text
naive:        ~0.0002 s
synchronized: ~0.021 s
```

Interpretation:

```text
naive timing
→ mostly CPU submission/dispatch time

synchronized timing
→ waits for MPS GPU work to complete
```

Key lesson:

```text
CPU submission time
≠
GPU execution/completion time
```

Use:

```python
torch.mps.synchronize()
```

before/after timing when measuring GPU completion from the host.

## Environment

Current local setup:

```text
M2 MacBook
├── Podman
├── PyTorch
├── CPU DDP + Gloo
└── MPS GPU timing
```

CUDA/NCCL-specific experiments require NVIDIA hardware.

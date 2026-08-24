# Training Lab

PyTorch distributed-training and GPU timing experiments.

## Files

```text
training/
├── train.py
├── dataloader_benchmark.py
├── gpu_inference_timing.py
└── README.md
```

## DDP Smoke Test

Run inside Podman:

```bash
podman run --rm -it \
  ghcr.io/syenpark/pytorch-ddp:latest \
  torchrun \
    --standalone \
    --nproc-per-node=2 \
    -m training.train
```

Here:

* `--standalone` runs the distributed job on one server/node and configures worker coordination automatically.
* `--nproc-per-node=2` starts two training processes on that node.

Validates:

* `torchrun`
* rank / world size
* process groups
* Gloo
* AllReduce

## DataLoader / CPU Contention Lab

Run with different worker counts:

```bash
podman run --rm -it \
  ghcr.io/syenpark/pytorch-ddp:latest \
  torchrun \
    --standalone \
    --nproc-per-node=4 \
    -m training.dataloader_benchmark \
    --num-workers 0
```

Then compare:

```bash
--num-workers 2
--num-workers 4
```

Increase CPU preprocessing cost with:

```bash
--work 500
```

Goal:

```text
too few workers
→ input starvation

appropriate workers
→ better throughput

too many workers
→ CPU contention / context switching
→ throughput may degrade
```

## Observe CPU Pressure

From another shell/container, inspect:

```bash
vmstat 1
pidstat -u 1
pidstat -w 1
top
```

Key troubleshooting principle:

```text
low GPU utilization
does not automatically mean
GPU bottleneck
```

Consider:

* DataLoader starvation
* CPU contention
* DDP synchronization / stragglers
* compute inefficiency
* infrastructure limits

## GPU Timing on M2

Run locally on macOS:

```bash
python training/gpu_inference_timing.py
```

Compare naive timing with:

```python
torch.mps.synchronize()
```

Key lesson:

```text
CPU submission time
≠
GPU completion time
```

## DistributedSampler Demo

Run with four distributed worker processes:

```bash
podman run --rm -it \
  ghcr.io/syenpark/pytorch-ddp:latest \
  torchrun \
    --standalone \
    --nproc-per-node=4 \
    -m training.distributed_sampler_demo
```

The demo creates a dataset containing 16 samples and uses `DistributedSampler` to give each worker a different subset. With four workers, each rank receives four samples.

## Environment

```text
M2 MacBook
├── Podman
│   └── Linux PyTorch DDP container
├── CPU DDP + Gloo
└── MPS GPU timing
```

Use the Linux Podman container for DDP experiments. Use MPS locally for GPU timing experiments.

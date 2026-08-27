# Training Lab

PyTorch distributed-training and GPU timing experiments.

## Files

```text
training/
├── train.py
├── dataloader_benchmark.py
├── distributed_sampler_demo.py
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

```text
torchrun
    |
    +-- process rank 0
    |
    +-- process rank 1
    |
    +-- process rank N
             |
             ↓
    dist.init_process_group()
             |
             ↓
       AllReduce
```

It proves:

*"Can multiple PyTorch processes communicate correctly?"*

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

Compare naive timing with synchronized timing:

```python
torch.mps.synchronize()
```

Key lesson:

```text
CPU submission time
≠
GPU completion time
```

A GPU operation may be asynchronous relative to the CPU:

```text
model(x)
→ returns a Tensor representing the result
→ GPU may still be producing that result asynchronously

y = model(x_gpu)
│
├── returns Tensor quickly
│
│        GPU still computing y...
│
y.cpu()
│
├── CPU needs result
│        ↓
│      WAIT
│        ↓
│      GPU finishes
│        ↓
└── correct data transferred/available
```

If GPU execution itself is only a few milliseconds but there are long idle gaps between batches, investigate the upstream pipeline first:

```text
DataLoader / CPU preprocessing
        ↓
Queue starvation
        ↓
H2D transfer
        ↓
GPU compute

                    GPU idle
                       ↑
                Why no work?
                 /          \
        batch not ready     transfer slow
             ↑                   ↑
       queue empty            H2D expensive
             ↑
       DataLoader slow

DataLoader creates the batch. Queue buffers the batch. H2D moves the batch onto the GPU.
```

Do not optimize TensorRT kernels before proving that GPU compute is the bottleneck.

On Apple Silicon, the device-transfer boundary is still useful conceptually, but the physical memory model differs from a discrete CUDA GPU connected over PCIe.

```python
batch_cpu = next(loader)     # DataLoader
batch_gpu = batch_cpu.to("cuda")  # H2D
output = model(batch_gpu)    # GPU compute
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

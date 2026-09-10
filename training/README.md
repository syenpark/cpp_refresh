# Training Lab

PyTorch distributed-training and GPU timing experiments.

## Contents

- [DDP Smoke Test](#ddp-smoke-test)
- [DataLoader / CPU Contention Lab](#dataloader--cpu-contention-lab)
- [DDP Straggler Lab](#ddp-straggler-lab)
- [One Training Iteration: CPU → GPU → DDP](#one-training-iteration-cpu--gpu--ddp)
- [GPU Synchronization vs Distributed Synchronization](#gpu-synchronization-vs-distributed-synchronization)
- [DDP Sync vs. Gradient Sync](#ddp-sync-vs-gradient-sync)
- [Training Performance Troubleshooting Map](#training-performance-troubleshooting-map)
- [Observe CPU Pressure](#observe-cpu-pressure)
- [GPU Timing on M2](#gpu-timing-on-m2)
- [DistributedSampler Demo](#distributedsampler-demo)
- [Environment](#environment)

## Files

```text
training/
├── __init__.py
├── Containerfile
├── train.py
├── dataloader_benchmark.py
├── dataloader_benchmark_straggler.py
├── distributed_sampler_demo.py
├── gpu_inference_timing.py
└── README.md
```

## DDP Smoke Test

Run inside Podman [./train.py](./train.py):

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

<details>
<summary>torchrun / process-group diagram</summary>

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
</details>

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

### DDP Ranks vs DataLoader Workers

`--nproc-per-node` and `DataLoader(num_workers=...)` control different processes.

<details>
<summary>Full breakdown</summary>

```text
torchrun --nproc-per-node=2
│
├── DDP rank 0 (training process)
│   └── DataLoader
│
└── DDP rank 1 (training process)
    └── DataLoader
```

With:

```text
--nproc-per-node=2
--num-workers=0
```

there are two DDP training processes and no additional DataLoader worker
processes. Each rank loads and preprocesses its own data.

With:

```text
--nproc-per-node=2
--num-workers=4
```

the structure becomes:

```text
DDP rank 0
├── DataLoader worker process 0
├── DataLoader worker process 1
├── DataLoader worker process 2
└── DataLoader worker process 3

DDP rank 1
├── DataLoader worker process 0
├── DataLoader worker process 1
├── DataLoader worker process 2
└── DataLoader worker process 3
```

Therefore:

```text
2 DDP ranks
+
2 × 4 DataLoader workers
=
2 training processes + 8 DataLoader worker processes
```

A DataLoader worker is a **process**, not a thread.

The two settings solve different problems:

```text
nproc-per-node
→ training parallelism / DDP ranks

num_workers
→ input pipeline parallelism within each rank
```

Increasing both blindly can oversubscribe the available CPUs.

For scaling experiments, keep `num_workers=0` initially so that changing the
number of DDP ranks is the main experimental variable.

</details>

---

## DDP Straggler Lab

Run the same DataLoader benchmark with one rank artificially slowed down, to
see what a single straggler does to synchronous DDP training:

```bash
podman run --rm -it \
  ghcr.io/syenpark/pytorch-ddp:latest \
  torchrun \
    --standalone \
    --nproc-per-node=4 \
    -m training.dataloader_benchmark_straggler \
    --num-workers 0
```

`--num-workers 0` keeps CPU contention out of the picture so that the
deliberate straggler is the only variable.

What [./dataloader_benchmark_straggler.py](./dataloader_benchmark_straggler.py)
does:

* builds the same synthetic dataset and DDP model as the non-straggler
  benchmark
* on the very first batch of the first epoch, rank 2 (`RANK = 2`) sleeps for
  0.5 s after its forward pass — the injected straggler
* every rank logs its `loader_time`, `forward_time`, and `backward_time` for
  that first batch
* rank 0 reduces the total number of processed samples and reports the global
  throughput

Visually:

```text
rank 0  forward → backward ─┐
rank 1  forward → backward ─┤
rank 2  forward → SLEEP → backward   ← injected straggler (0.5 s)
rank 3  forward → backward ─┘
                              ↓
          DDP gradient sync waits for rank 2
                              ↓
                  global throughput decreases
```

Things to notice:

* one slow rank paces the whole job: the other ranks finish their own backward
  pass but still block at the gradient synchronization until rank 2 catches up
* the injected 0.5 s sits **between** the logged stages, so it is not captured
  by any rank's per-stage timers — it shows up in the end-to-end `elapsed` and
  in the reduced global throughput
* run the same workload with
  `-m training.dataloader_benchmark` (no straggler) to compare the throughput
  of the identical sync setup

This is the last branch of the
[Training Performance Troubleshooting Map](#training-performance-troubleshooting-map):

```text
Are some ranks slower than others?
    └── YES → straggler / DDP synchronization / communication
```

---

## One Training Iteration: CPU → GPU → DDP

A useful mental model is to follow one batch through the system:

```text
Storage / Dataset
       │
       ▼
DataLoader workers              CPU / storage
       │
       │ read / decode / preprocess
       ▼
prefetch queue
       │
       ▼
next(loader)
       │
       ▼
CPU batch
       │
       │ .to("cuda")
       ▼
GPU batch                       CPU → GPU
       │
       ▼
forward                         GPU
       │
       ▼
loss
       │
       ▼
backward                        GPU
       │
       │ gradients become ready
       ▼
DDP gradient synchronization    rank/GPU ↔ rank/GPU
       │
       ▼
optimizer.step()
       │
       ▼
next iteration
```

A simplified training loop is:

```python
for x_cpu, y_cpu in loader:
    x = x_cpu.to("cuda")
    y = y_cpu.to("cuda")

    optimizer.zero_grad()

    prediction = model(x)       # forward
    loss = loss_fn(prediction, y)
    loss.backward()             # backward + DDP gradient synchronization
    optimizer.step()
```

### Where is the CPU involved?

Primarily in the upstream input pipeline:

```text
Dataset
→ DataLoader
→ decoding / preprocessing
→ batching / prefetching
```

If this pipeline cannot produce batches fast enough:

```text
DataLoader slow
→ queue becomes empty
→ next(loader) waits
→ GPU receives no new work
→ GPU starvation
→ GPU utilization decreases
```

Low GPU utilization therefore does **not** automatically mean slow GPU
compute.

### Where is the GPU involved?

When the model and tensors are on the GPU, the major numerical work for both
passes happens there:

```text
GPU
│
├── forward
│   ├── matrix multiplication
│   ├── convolution
│   └── activation
│
└── backward
    └── gradient computation
```

The backward pass computes gradients for model parameters.

With DDP, gradients must also be combined across ranks. PyTorch DDP normally
uses collective communication such as AllReduce for this.

Conceptually:

```text
rank 0 / GPU 0     rank 1 / GPU 1     rank 2 / GPU 2
      │                   │                   │
   forward             forward             forward
      │                   │                   │
   backward            backward            backward
      │                   │                   │
 gradients            gradients           gradients
      └──────────── AllReduce ────────────────┘
                       │
                       ▼
              synchronized gradients
```

In practice, DDP can overlap gradient communication with backward computation
as gradient buckets become ready.

---

## GPU Synchronization vs Distributed Synchronization

Do not confuse local GPU synchronization with synchronization between DDP
ranks.

### `torch.cuda.synchronize()`

```python
torch.cuda.synchronize()
```

means:

```text
CPU
 │
 │ wait
 ▼
local GPU finishes previously submitted CUDA work
```

CUDA operations are generally asynchronous relative to the CPU. This is why
GPU timing often requires:

```python
torch.cuda.synchronize()
start = time.perf_counter()

output = model(x)

torch.cuda.synchronize()
elapsed = time.perf_counter() - start
```

Without synchronization, the CPU timer may measure mainly submission/dispatch
time rather than completed GPU execution.

On MPS, the analogous operation used in this lab is:

```python
torch.mps.synchronize()
```

### `dist.barrier()`

```python
dist.barrier()
```

means:

```text
rank 0 ─────────────┐
rank 1 ───────┐     │
rank 2 ──────────┐  │
                 ▼  ▼
              barrier
                 │
         wait for all ranks
                 │
                 ▼
          all ranks continue
```

It synchronizes **distributed processes**, not CPU execution with local GPU
completion.

```text
torch.cuda.synchronize()
→ host waits for local CUDA work

dist.barrier()
→ rank waits for other ranks

DDP AllReduce
→ ranks communicate/combine gradients
```

`dist.barrier()` is not what DDP normally uses to synchronize gradients after
every backward pass. Gradient synchronization uses collectives such as
AllReduce.

---

## DDP Sync vs. Gradient Sync

Inside DDP training, "synchronization" appears in two distinct forms:

* **process-group sync** — `dist.barrier()`: all ranks block until every rank
  arrives. No data is moved.
* **gradient sync** — DDP's per-bucket AllReduce: each rank's local gradients
  are communicated and combined so every rank ends with identical averaged
  gradients. Data is moved and reduced.

```text
dist.barrier()                              DDP gradient sync
(rendezvous, no data moved)                 (communication + arithmetic)

rank 0 ─────────────┐                       rank 0  grad_0 ─┐
rank 1 ───────┐     │                       rank 1  grad_1 ─┤
rank 2 ──────────┐  │                       rank 2  grad_2 ─┤
                 ▼  ▼                       rank 3  grad_3 ─┘
              barrier                                  │
                 │                        AllReduce per  │
         wait for all ranks               bucket        │
                 │                                      ▼
                 ▼                          same averaged gradients
          all ranks continue                on every rank
```

### `dist.barrier()` — rendezvous, not data exchange

```python
dist.barrier()
```

* imposes ordering: nothing after the barrier runs until every rank arrives
* moves no model state or gradients
* in this lab it brackets the timing window — the straggler script calls it
  around the measured loop; it aligns the start/end of the measurement, it
  does not synchronize gradients

### Gradient sync — automatic during backward

With `DistributedDataParallel`, `loss.backward()` does more than compute local
gradients:

```text
loss.backward()
     |
     +-- autograd: compute local gradients for each parameter
     |
     +-- reducer: as each gradient bucket becomes ready,
     |            launch AllReduce to combine it across ranks
     |
     v
     all ranks hold identical averaged gradients
     |
     v
     optimizer.step() applies the same update on every rank
```

* reduction is **overlapped** with the remaining backward computation: an
  early bucket can be all-reduced while later buckets are still being computed.
  It is not a "backward, then barrier" sequence.
* no explicit `dist.barrier()` is needed for gradient sync — `loss.backward()`
  triggers it
* calling `dist.all_reduce()` on a gradient manually on top of DDP would
  combine already-reduced values again (double reduction)

### Why a straggler stalls the whole job

In the [DDP Straggler Lab](#ddp-straggler-lab), it is the gradient sync, not a
barrier, that paces the run:

```text
slow rank's bucket is late
           |
           v
AllReduce cannot complete
           |
           v
every rank's optimizer.step() is delayed
           |
           v
global throughput decreases
```

Ranks 0, 1, and 3 finish their backward pass, but their AllReduce cannot
complete until rank 2's gradient bucket arrives. The gradient sync makes all
ranks move at the pace of the slowest rank.

Mental model:

```text
dist.barrier()
→ "wait until everyone is here" — ordering, no data

DDP gradient AllReduce
→ "combine everyone's gradients" — data + arithmetic

when ranks move at different speeds,
the gradient AllReduce pins all ranks to the slowest one
```

---

## Training Performance Troubleshooting Map

Follow the batch through the pipeline instead of assuming that low throughput
means a GPU problem.

```text
Throughput low
     │
     ▼
Is next(loader) slow?
     │
     ├── YES → DataLoader / CPU / storage / queue starvation
     │
     ▼ NO
Is .to(device) slow?
     │
     ├── YES → device-transfer bottleneck
     │
     ▼ NO
Is synchronized forward/backward slow?
     │
     ├── YES → GPU compute bottleneck
     │
     ▼ NO
Are some ranks slower than others?
     │
     └── YES → straggler / DDP synchronization / communication
```

Useful measurements:

```python
t0 = time.perf_counter()
batch_cpu = next(loader)
t1 = time.perf_counter()

batch_gpu = batch_cpu.to(device)
# synchronize device when measuring asynchronous GPU transfer/work
t2 = time.perf_counter()

output = model(batch_gpu)
# synchronize device when measuring asynchronous GPU work
t3 = time.perf_counter()
```

Then investigate the stage that actually consumes the time rather than
optimizing the GPU first.

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

## GPU Timing and Profiling

Run locally on macOS:

```bash
python training/gpu_inference_timing.py
```

Profile the CPU preprocessing, device transfer, and synchronized forward
stages. The default selects CUDA when available, otherwise MPS:

```bash
python training/gpu_inference_profiling.py
```

Select a backend explicitly:

```bash
# Apple Silicon / MPS
python training/gpu_inference_profiling.py --device mps

# NVIDIA GPU / CUDA
python training/gpu_inference_profiling.py --device cuda
```

The MPS run records an MPS Instruments trace. The CUDA run uses
`torch.profiler` and writes `gpu_inference_cuda_trace.json`, which can be
opened in Chrome tracing or TensorBoard.

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

<details>
<summary>Why `y.cpu()` may WAIT</summary>

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
</details>

If GPU execution itself is only a few milliseconds but there are long idle gaps between batches, investigate the upstream pipeline first:

<details>
<summary>Where the pipeline stalls</summary>

```text
DataLoader workers / CPU preprocessing
      ↓
prefetch queue
      ↓
next(loader)
      ↓
CPU batch
      ↓
.to(device)        ← H2D/device-transfer boundary
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
</details>

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

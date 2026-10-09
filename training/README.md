# Training Lab

PyTorch distributed-training and GPU timing experiments.

## Contents

- [Without vs With DistributedSampler](#without-vs-with-distributedsampler)
- [DataLoader / CPU Contention Lab](#dataloader--cpu-contention-lab)
- [DDP Straggler Lab](#ddp-straggler-lab)
- [One Training Iteration: CPU → GPU → DDP](#one-training-iteration-cpu--gpu--ddp)
- [GPU Synchronization vs Distributed Synchronization](#gpu-synchronization-vs-distributed-synchronization)
- [DDP Sync vs. Gradient Sync](#ddp-sync-vs-gradient-sync)
- [Training Performance Troubleshooting Map](#training-performance-troubleshooting-map)
- [Observe CPU Pressure](#observe-cpu-pressure)
- [GPU Timing and Profiling](#gpu-timing-and-profiling)
- [Environment](#environment)
- [ddp_buckets for DDP concept](#ddp_buckets-for-ddp-concept)

## Files

```text
training/
├── __init__.py
├── Containerfile
├── dataloader_benchmark.py
├── dataloader_benchmark_straggler.py
├── gpu_inference_timing.py
├── gpu_inference_profiling.py
└── README.md
```

## Without vs With DistributedSampler

*Does skipping `DistributedSampler` mean every rank just re-processes the
same data?*

[./dataloader_benchmark.py](./dataloader_benchmark.py) answers this, and
along the way doubles as the DDP smoke test: starting it already requires
`torchrun`, rank/world size, process groups, Gloo, and an AllReduce-style
`dist.barrier()`/`dist.reduce()` to all work.

### Step 1 — start the process group

```bash
uv run torchrun --nproc-per-node=2 --master_addr=127.0.0.1 --master_port=29500 \
  -m training.dataloader_benchmark --no-sampler
```

* `--nproc-per-node=2` starts two training processes (ranks 0 and 1) on this
  machine.
* each rank calls `dist.init_process_group(backend="gloo")`, gets a `rank`
  and `world_size`, and only then is DDP "live".

This alone proves *"can multiple PyTorch processes communicate correctly?"* —
regardless of what the `DataLoader` does next.

### Step 2 — without `DistributedSampler` (`--no-sampler`)

With no sampler, `DataLoader` has no way to know a rank only owns part of the
dataset, so every rank iterates all of it from index 0:

```text
rank=0 first batch x[:3]=[...]
rank=1 first batch x[:3]=[...]   ← identical to rank 0
rank=0 total_samples=<dataset-size>
rank=1 total_samples=<dataset-size>   ← same as rank 0, not split
```

Both ranks log the *same* first-batch values and the *same* `total_samples`
— each rank redundantly trained on the whole dataset.

### Step 3 — with `DistributedSampler` (the default, no flag needed)

```bash
uv run torchrun --nproc-per-node=2 --master_addr=127.0.0.1 --master_port=29500 \
  -m training.dataloader_benchmark
```

`DistributedSampler(dataset, num_replicas=world_size, rank=rank)` hands each
rank a disjoint slice of indices, and `sampler.set_epoch(epoch)` re-shuffles
that slice per epoch:

```text
rank=0 first batch x[:3]=[...]   ← differs from rank 1
rank=1 first batch x[:3]=[...]
rank=0 total_samples=<dataset-size / world-size>
rank=1 total_samples=<dataset-size / world-size>   ← together, the full dataset
```

The two ranks now log *different* first-batch values, and each `total_samples`
is roughly `1/world_size` of the full dataset — together the ranks cover it
exactly once.

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

Mental model:

```text
without DistributedSampler            with DistributedSampler
rank 0 → full dataset, index 0..N     rank 0 → shard A (disjoint slice)
rank 1 → full dataset, index 0..N     rank 1 → shard B (disjoint slice)
   (duplicate work, same data)           (each sample seen once per epoch)
```

So what:

* without a sampler, DDP does not crash and gradients still average fine —
  the bug is silent duplicated work, not a visible failure
* `DistributedSampler` + `sampler.set_epoch(epoch)` is what actually turns
  "N ranks" into "N-way data parallelism"; skipping either one quietly turns
  a distributed job back into N redundant copies of a single-process job

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

CUDA operations are generally asynchronous relative to the CPU. Why:

<details>
<summary>Why CUDA is async relative to the CPU</summary>

A CUDA call usually does not run on the CPU — it *submits* work to the GPU's
command queue (via the driver), and the call returns as soon as the submission
is accepted. The GPU then executes the queued work on its own cores.

```text
CPU                          GPU
───                          ───
model(x) → enqueue kernels →
returns immediately              ... executing kernels ...
prepare next batch (overlap)
```

The host is only forced to wait when it actually needs the result:
copying data back to the CPU (`.cpu()`), calling synchronize, or a later
operation in the same stream that depends on the queued work. This is
intentional design: it lets the CPU stay ahead of the GPU, keeping the GPU
fed instead of stalling it while the CPU prepares the next batch.
</details>

This is why GPU timing often requires:

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

[Colab Tensorboard GPU profiling example](https://colab.research.google.com/drive/1zIQs4xS_cmJJhHvyKpmmPW6xXtTXgH5Y#scrollTo=iCT4ynRMDmuF)

## Environment

```text
M2 MacBook
├── Podman
│   └── Linux PyTorch DDP container
├── CPU DDP + Gloo
└── MPS GPU timing
```

Use the Linux Podman container for DDP experiments. Use MPS locally for GPU timing experiments.

### ddp_buckets for DDP concept

[./ddp_buckets](./ddp_buckets/) includes the following examples:

| # | File | Run | What to look at |
| --- | ------ | ----- | ----------------- |
| 1 | c1_ddp_buckets.py | `torchrun --nproc_per_node=2 c1_ddp_buckets.py [--bucket-cap-mb 1] [--grad-accum 4 [--no-sync]]` | broadcast at construction, bucket order, hook times inside backward, all-reduce count |
| 2 | c2_ring_allreduce.py | `torchrun --nproc_per_node=4 c2_ring_allreduce.py` | bytes sent = 2(N-1)/N x S, result matches dist.all_reduce |
| 3 | c3_cost_model.py | `python c3_cost_model.py` | when comm stops hiding behind backward; FSDP memory |
| 4 | c4_fsdp_by_hand.py | `torchrun --nproc_per_node=4 c4_fsdp_by_hand.py` | sharded weights give the same answer as DDP with 1/N memory |
| 5 | c5_hang_demo.py | `torchrun --nproc_per_node=2 c5_hang_demo.py --mode skip` | a skipped collective = hang -> timeout error |

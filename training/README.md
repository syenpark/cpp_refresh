# Training Lab

PyTorch distributed-training and GPU timing experiments.

## How to Run

```text
macOS (host):   uv run torchrun --nproc-per-node=N --master_addr=127.0.0.1 \
                  --master_port=29500 -m training.<module>

Podman (Linux): podman run --rm -it ghcr.io/syenpark/pytorch-ddp:latest \
                  torchrun --standalone --nproc-per-node=N -m training.<module>
```

```text
training/
├── dataloader_benchmark.py            DistributedSampler on/off + DDP smoke test
├── dataloader_benchmark_straggler.py  one slow rank vs DDP gradient sync
├── gpu_inference_timing.py            naive vs synchronized GPU timing (MPS)
├── gpu_inference_profiling.py         torch.profiler / MPS trace (CUDA or MPS)
├── ddp_buckets/                       bucket / ring-allreduce / FSDP micro-examples
└── Containerfile                      image used by the torchrun examples above
```

## Part 0 — Core Concepts

### What do weights, loss, gradients, and optimizer each do?

*What is the one training step actually doing?*

```bash
uv run torchrun --nproc-per-node=2 --master_addr=127.0.0.1 --master_port=29500 \
  -m training.ddp_buckets.c1_ddp_buckets
```

```text
one step, same on every rank:

  x ──forward(weights)──▶ y ──▶ loss = one number (how far y is from target)
                               │
  weights ──gradient───────────┘   backward(): d(loss)/d(each weight),
        │                           stored in weight.grad (NOT a new weights)
        ▼
  optimizer.step():   weight -= lr × gradient     (Adam etc.)
  optimizer.zero_grad()

weights = the memory of the model
loss    = a score, computed by forward, never used to update anything itself
gradient= the "which way to nudge", computed by backward
optimizer= the only thing that actually moves the weights
```

- the optimizer never sees loss or forward — it consumes `.grad` and updates
  weights. That's why all DDP needs to average is the gradients.
- `zero_grad` matters: `.grad` accumulates across steps, so skipping it adds
  old gradients into the next update.

### What are DDP hooks and buckets?

*Why does DDP start all-reduce in the middle of `backward()` instead of
waiting until it finishes?*

```bash
uv run torchrun --nproc-per-node=2 --master_addr=127.0.0.1 --master_port=29500 \
  -m training.ddp_buckets.c1_ddp_buckets --bucket-cap-mb 1    # many small buckets
```

What you see: the comm hook fires several times inside one `loss.backward()`
call, each firing = one bucket's all-reduce starting.

```text
backward() fills gradients last-layer-first:

  grad L8 ─▶ grad L7 ─▶ grad L6 ─▶ ... ─▶ grad L1
       │              │
       └─ bucket (≤ ~25 MB of grads) fills up → HOOK fires right there
          → all-reduce(that bucket) starts on a background stream
          → rest of backward keeps computing
```

- **hook** = a callback DDP attaches to leaf params: "your grad is filled,
  you can ship now". It fires inside `backward()`, once per ready bucket.
- **bucket** = a group of params whose grads are all-reduced together (default
  25 MB). One all-reduce per bucket, not per param — batching cuts per-message
  overhead.
- because each bucket ships as soon as it's ready, communication overlaps the
  remaining backward — comm is mostly hidden. c3's cost model shows when it
  stops being hidden.

## Part 1 — How DDP Works

### Who does what

*Which component splits the data, and which one syncs the model?*

```text
torchrun --nproc-per-node=2        (spawns ranks + sets env, then just watches)
   ├───────────────────────────────┐
   ▼                               ▼
rank 0                           rank 1
┌─ DataLoader ────────────┐      ┌─ DataLoader ────────────┐
│ sampler  → [0, 2, 4, …] │      │ sampler  → [1, 3, 5, …] │  ← split by math, no comm
│ dataset[i] → 1 sample   │      │ dataset[i] → 1 sample   │
│ collate ×B → batch      │      │ collate ×B → batch      │
└───────────┬─────────────┘      └───────────┬─────────────┘
            ▼                                ▼
   DDP(model) fwd + bwd             DDP(model) fwd + bwd
            │ ◀──── AllReduce(grad) ────▶    │               ← the ONLY comm
            ▼                                ▼
     optimizer.step()                 optimizer.step()       → identical weights
```

| Component | Job | In `dataloader_benchmark.py` |
| --- | --- | --- |
| torchrun | launcher: spawns N copies, sets `RANK` / `WORLD_SIZE` / `MASTER_ADDR` / `MASTER_PORT` | `--nproc-per-node=4` |
| Sampler | which indices this rank takes, in what order | `DistributedSampler(dataset, num_replicas=world_size, rank=rank)` |
| Dataset | ONE sample for index `i` — never a batch | `SyntheticDataset` |
| DataLoader | drives the loop: sampler → `dataset[i]` → collate into a batch | `DataLoader(dataset, ..., sampler=sampler)` |
| DDP | wraps the model; all-reduces **gradients** during `backward()` | `DistributedDataParallel(model)` |

- torchrun does NOT split data (sampler), average gradients (DDP), or move the
  model to the GPU (my code).
- `batch_size`, `shuffle`, `sampler`, `num_workers` are DataLoader settings,
  not Dataset ones.
- DDP averages gradients, not weights. Weights stay identical because DDP
  broadcasts rank 0's weights once at construction, then every rank applies
  the same averaged gradient with the same optimizer.

```text
epoch
 └── step = optimizer.step() on one batch     global_batch = batch_size × world_size

e.g. 2000 samples, 4 ranks, batch_size=32
with sampler:    2000 / 4 = 500 per rank → ceil(500/32) = 16 steps per rank per epoch
without sampler: each rank sees all 2000 → ceil(2000/32) = 63 steps (4× the work, same model)
global batch = 32 × 4 = 128 samples per optimizer.step()
```

### Without vs With DistributedSampler

*Does skipping `DistributedSampler` mean every rank just re-processes the
same data?*

```bash
uv run torchrun --nproc-per-node=2 --master_addr=127.0.0.1 --master_port=29500 \
  -m training.dataloader_benchmark --no-sampler      # every rank: full dataset

uv run torchrun --nproc-per-node=2 --master_addr=127.0.0.1 --master_port=29500 \
  -m training.dataloader_benchmark                   # default: disjoint shards
```

```text
┌─ --no-sampler ─────────────────┐   ┌─ default (with sampler) ───────┐
│ rank 0 → full dataset          │   │ rank 0 → 1/N of dataset        │
│ rank 1 → full dataset          │   │ rank 1 → 1/N of dataset        │
│  = duplicate work, same data   │   │  = disjoint, dataset once      │
│                                │   │                                │
│ +ranks       → NO speedup      │   │ +ranks       → ≈linear speedup │
│ SUM(throughput) → inflated N×  │   │ 4 ranks      → ≈4× faster      │
└────────────────────────────────┘   └────────────────────────────────┘
```

```text
no sampler → every rank gets identical batches (same order: shuffle=False) → identical gradients
→ average of identical gradients = the same gradient
→ same model as 1 rank, no speedup; no error is raised
```

```text
x[:3] converges to ≈1.2587 for almost every index (sin+cos iteration) —
look at indices/total_samples above to tell ranks apart, not x.

sampler.set_epoch(epoch) runs either way, but only reshuffles when
shuffle=True — here shuffle=False, so it has no visible effect.
```

### One Training Iteration: CPU → GPU → DDP

```text
Dataset → DataLoader → CPU batch ──.to("cuda")──▶ GPU batch
                                                      │
                                                   forward
                                                      │
                                                    loss
                                                      │
                                   backward  (+ DDP AllReduce, overlapped)
                                                      │
                                              optimizer.step()

DataLoader slow → queue empty → next(loader) waits → GPU starves
                                        (low GPU util ≠ GPU bottleneck)
```

<details>
<summary>Why CUDA is asynchronous relative to the CPU</summary>

```text
CPU                          GPU
model(x) → enqueue kernels →
returns immediately              ... executing kernels ...

host only blocks on: .cpu(), explicit synchronize(), or a dependent op
in the same stream
```
</details>

### Three Kinds of "Sync"

```text
torch.cuda/mps.synchronize()   dist.barrier()             DDP AllReduce (in backward())
host ↔ own GPU work            rank ↔ all other ranks      rank ↔ all other ranks
wait only, no data moved       wait only, no data moved    wait + combine gradients
─────────────────────────────  ──────────────────────────  ──────────────────────────
CUDA calls are async: an       explicit rendezvous: every   a straggler stalls everyone
un-synced timer measures       rank waits until the last    HERE, not at a barrier
submission, not completion     one arrives                  (see Straggler Lab)
                                                            this lab's dist.reduce(dst=0,
                                                            SUM) is reduce-to-one — only
                                                            rank 0 gets it
```

### Precision checks

```text
"DDP averages weights"             → DDP averages GRADIENTS; weights stay equal as a result
"Dataset returns a batch"          → Dataset returns ONE sample; DataLoader makes batches
"torchrun splits the data"         → torchrun launches + sets RANK/WORLD_SIZE/MASTER_ADDR/PORT; the sampler splits
"no sampler → error / wrong model" → no error; same model as 1 rank, just no speedup
"barrier = all-reduce"             → barrier only waits; all-reduce waits AND combines
```

## Part 2 — Why Training Is Slow

### CPU Contention Lab

*Does adding more DataLoader workers always help?*

```bash
podman run --rm -it ghcr.io/syenpark/pytorch-ddp:latest \
  torchrun --standalone --nproc-per-node=4 \
  -m training.dataloader_benchmark --num-workers 0   # then try 2, 4; --work 500
```

```text
too few workers  → input starvation
right number     → better throughput
too many         → CPU contention / context switching → throughput may drop
```

<details>
<summary>DDP ranks vs DataLoader workers — different processes</summary>

```text
torchrun --nproc-per-node=2
├── DDP rank 0 (process) → DataLoader(num_workers=4) → 4 worker processes
└── DDP rank 1 (process) → DataLoader(num_workers=4) → 4 worker processes
= 2 training processes + 8 DataLoader worker processes (a worker is a
  process, not a thread)

keep num_workers=0 while scaling --nproc-per-node, so ranks are the only
variable
```
</details>

### Straggler Lab

*What does one slow rank do to synchronous DDP?*

```bash
podman run --rm -it ghcr.io/syenpark/pytorch-ddp:latest \
  torchrun --standalone --nproc-per-node=4 \
  -m training.dataloader_benchmark_straggler --num-workers 0
```

```text
rank 0,1,3  forward ───────────▶ backward ─┐
rank 2      forward → SLEEP(0.5s) → backward   ← injected straggler
                                             ↓
                   AllReduce waits for rank 2 → global throughput drops

per-rank loader_time/forward_time/backward_time timers each miss the 0.5s
(it sits between forward and backward) — only elapsed/throughput show it.

Compare against -m training.dataloader_benchmark (no straggler, same
world size) to see the gap.
```

### Training Performance Troubleshooting Map

```text
Throughput low
  ├── next(loader) slow?            → DataLoader / CPU / storage starvation
  ├── .to(device) slow?             → device-transfer bottleneck
  ├── forward/backward slow?        → GPU compute bottleneck
  └── one rank slower than others?  → straggler / DDP sync (Part 1)
```

```text
observe CPU pressure:  vmstat 1 | pidstat -u 1 | pidstat -w 1 | top
low GPU utilization does NOT automatically mean a GPU bottleneck
```

## Part 3 — GPU Timing and Profiling

*Does a fast-returning GPU call mean the GPU actually finished?*

```bash
uv run python training/gpu_inference_timing.py
uv run python training/gpu_inference_profiling.py        # CUDA if available, else MPS
uv run python training/gpu_inference_profiling.py --device mps   # or --device cuda
```

```text
y = model(x_gpu)   → returns quickly, GPU still computing
y.cpu()             → CPU blocks until GPU actually finishes

CPU submission time ≠ GPU completion time

MPS run  → Instruments trace
CUDA run → gpu_inference_cuda_trace.json (chrome://tracing / TensorBoard)
```

<details>
<summary>Where the pipeline stalls, when GPU compute is fast but batches
arrive slowly</summary>

```text
DataLoader → prefetch queue → next(loader) → CPU batch → .to(device) → GPU compute
                                   GPU idle? → queue empty (DataLoader slow)
                                             → or H2D transfer itself slow
```
</details>

[Colab TensorBoard GPU profiling example](https://colab.research.google.com/drive/1zIQs4xS_cmJJhHvyKpmmPW6xXtTXgH5Y#scrollTo=iCT4ynRMDmuF)

## Appendix

```text
Environment
  M2 MacBook
  ├── Podman → Linux PyTorch DDP container (CPU DDP + Gloo)
  └── MPS GPU timing (local, no container)
```

[./ddp_buckets](./ddp_buckets/) for DDP concepts:

| # | File | Run | What to look at |
| --- | ------ | ----- | ----------------- |
| 1 | c1_ddp_buckets.py | `uv run torchrun --nproc-per-node=2 --master_addr=127.0.0.1 --master_port=29500 -m training.ddp_buckets.c1_ddp_buckets [--bucket-cap-mb 1] [--grad-accum 4 [--no-sync]]` | broadcast at construction, bucket order, hook times inside backward, all-reduce count |
| 2 | c2_ring_allreduce.py | `uv run torchrun --nproc-per-node=4 --master_addr=127.0.0.1 --master_port=29500 -m training.ddp_buckets.c2_ring_allreduce` | bytes sent = 2(N-1)/N x S, result matches dist.all_reduce |
| 3 | c3_cost_model.py | `uv run python -m training.ddp_buckets.c3_cost_model` | when comm stops hiding behind backward; FSDP memory |
| 4 | c4_fsdp_by_hand.py | `uv run torchrun --nproc-per-node=4 --master_addr=127.0.0.1 --master_port=29500 -m training.ddp_buckets.c4_fsdp_by_hand` | sharded weights give the same answer as DDP with 1/N memory |
| 5 | c5_hang_demo.py | `uv run torchrun --nproc-per-node=2 --master_addr=127.0.0.1 --master_port=29500 -m training.ddp_buckets.c5_hang_demo --mode skip` | a skipped collective = hang -> timeout error |

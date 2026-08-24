# Training Lab

PyTorch distributed-training and GPU timing experiments.

<!-- markdownlint-disable MD033 -->
<details>
<summary><b>Files</b></summary>

```text
training/
├── train.py
├── dataloader_benchmark.py
├── distributed_sampler_demo.py
├── gpu_inference_timing.py
└── README.md
```

</details>

<details>
<summary><b>DDP Smoke Test</b></summary>

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

```bash
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

"Can multiple PyTorch processes communicate correctly?"
</details>

<details>
<summary><b>DataLoader / CPU Contention Lab</b></summary>

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

</details>

<details>
<summary><b>Observe CPU Pressure</b></summary>

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

</details>

<details>
<summary><b>GPU Timing on M2</b></summary>

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

</details>

<details>
<summary><b>DistributedSampler Demo</b></summary>

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
</details>

<details>
<summary><b>Environment</b></summary>

```text
M2 MacBook
├── Podman
│   └── Linux PyTorch DDP container
├── CPU DDP + Gloo
└── MPS GPU timing
```

Use the Linux Podman container for DDP experiments. Use MPS locally for GPU timing experiments.
</details>
<!-- markdownlint-enable MD033 -->

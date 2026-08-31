# A Systems-Oriented Engineer's Lab: C++, Python/PyTorch, Kubernetes

[![C++ Pre-commit Checks](https://github.com/syenpark/cpp_refresh/actions/workflows/cpp-precommit.yml/badge.svg)](https://github.com/syenpark/cpp_refresh/actions/workflows/cpp-precommit.yml)

A hands-on lab across the layers of modern ML infrastructure — low-latency **C++** kernels, **Python/PyTorch** distributed training, and **Kubernetes** orchestration.

Modern ML systems are increasingly limited not only by model inference, but also by everything around the model:

- data movement
- serialization/deserialization
- memory behavior
- CPU efficiency
- process communication
- resource utilization

From C++ memory management and lock-free queues, to DDP process groups and pod scheduling, this repo builds components explicitly from the ground up. The goal is not to hide complexity behind frameworks, but to build intuition about what happens underneath them.

*Note: the repository retains its original name, `cpp_refresh`, but the content scope is much broader.*

## Contents

- [Repository Layout](#repository-layout)
- [Download the Built Images](#download-the-built-images)
- [Create the Linux Systems Lab Cluster on Mac M2](#create-the-linux-systems-lab-cluster-on-mac-m2)
- [Practical Application: Video Analytics](#practical-application-video-analytics)
- [Learning Notes](#learning-notes)

## Repository Layout

| Path | What it is | Guide |
| --- | --- | --- |
| [`src/cpp/`](./src/cpp/) | C++ analytics bootstrap (CMake, ZeroMQ, config, JSON decode) | [src/cpp/README.md](./src/cpp/README.md) |
| [`training/`](./training/) | PyTorch distributed-training & GPU timing experiments | [training/README.md](./training/README.md) |
| [`k8s/`](./k8s/) | Local Kubernetes (Job vs Deployment) lab on kind + Podman | [k8s/README.md](./k8s/README.md) |
| [`linux/`](./linux/) | Base Linux Systems Lab container image | [Containerfile](./linux/Containerfile) |
| [`docs/`](./docs/) | Systems notes and learning docs | [jargon](./docs/jargon.md) · [memory layout](./docs/memory-hierarchy.md) · [allocators & cache](./docs/allocators-cache.md) |

### C++ source topics

`src/cpp/` also contains a series of focused, self-contained topics:

- [day01](./src/cpp/day01/) — stack vs heap, object lifetime
- [day02](./src/cpp/day02/) — reference vs copy
- [day03](./src/cpp/day03/) — elide vs move vs copy
- [day04](./src/cpp/day04/) — STL containers & API design
- [day05](./docs/allocators-cache.md) — allocators & cache behavior
- [day06](./src/cpp/day06/) — NUMA (numa.cpp)
- [day07](./src/cpp/day07/) — atomic vs mutex
- [week02](./src/cpp/week02/) — memory ordering (SPSC)
- [week03](./src/cpp/week03/) — lock-free queue (Treiber stack)
- [week04](./src/cpp/week04/) — micro benchmark (vector vs string)
- [lab](./src/cpp/lab/) — producer / consumer

## Download the Built Images

The [`Build Linux Systems Lab`](./.github/workflows/build-cpp-env.yml) workflow builds the image and publishes it to the GitHub Container Registry when [`linux/Containerfile`](./linux/Containerfile) changes are pushed to `main`.

The [`Build PyTorch DDP Container`](./.github/workflows/build-training-env.yml) workflow builds the training image and publishes it when the `training/` files change.

Sign in to GHCR with a GitHub token that has package read access, then pull the latest images:

```bash
echo "$GITHUB_TOKEN" | podman login ghcr.io -u YOUR_GITHUB_USERNAME --password-stdin
podman pull ghcr.io/syenpark/linux-cpp-env:latest
podman pull ghcr.io/syenpark/pytorch-ddp:latest
```

Run the Linux Systems Lab image with the repository mounted at `/workspace`:

```bash
podman run --rm -it \
    -v "$PWD:/workspace" \
    --cap-add=SYS_PTRACE \
    ghcr.io/syenpark/linux-cpp-env:latest
```

Run the PyTorch DDP training image with two processes:

```bash
podman run --rm -it \
    --network host \
    ghcr.io/syenpark/pytorch-ddp:latest \
    torchrun --standalone --nproc-per-node=2 -m training.train
```

## Create the Linux Systems Lab Cluster on Mac M2

Install kind and kubectl with Homebrew:

```bash
brew install kind kubectl
```

Create a kind cluster using Podman as the container provider:

```bash
KIND_EXPERIMENTAL_PROVIDER=podman kind create cluster --name mle-lab
```

Verify the cluster using its kubectl context:

```bash
kubectl cluster-info --context kind-mle-lab
```

For Kubernetes lab practice, see the [Kubernetes Lab](./k8s/README.md).

## Practical Application: Video Analytics

PyTorch distributed-training and M2 GPU timing experiments live in [`training/`](./training/). See the [training guide](./training/README.md) for details.

As model inference becomes faster and more efficient, the true bottleneck often shifts to data flow and real-time decision-making within the Python-based container. Python's inherent inefficiencies in the post-processing layer, especially on hot paths, can significantly hinder performance.

In AI-heavy applications—such as video streaming, autonomous driving, smart cities, and trading—optimizing everything beyond inference is critical. C++ plays a key role in eliminating these inefficiencies and squeezing out those final milliseconds. Therefore, I will simulate the Python hot path for processing object detection metadata and re-implement it in C++ to achieve the performance gains needed for real-time applications.

Install the Python dependencies and run the example locally with uv:

```bash
uv sync
uv run train
```

Run the M2 GPU timing example:

```bash
uv run python training/gpu_inference_timing.py
```

For multi-process execution, launch it with `torchrun` and set the desired process count:

```shell
torchrun --standalone --nproc-per-node=2 -m training.train
```

The training image is built by the [`Build PyTorch DDP Container`](./.github/workflows/build-training-env.yml) workflow. CI builds the project wheel with uv and installs the wheel in the container.

### Hot loop

Why the C++ Rewrite?

The Python implementation suffers from significant overhead in the "hot loop" due to:

1. Pointer Chasing: Python's memory model requires multiple heap lookups (List → Object → Dict → Value), leading to frequent cache misses.
2. Dynamic Lookups: Every attribute access (`obj.bbox`) triggers a hash table lookup in the instance dictionary.
3. Memory Fragmentation: Python objects are scattered across the heap, preventing the CPU hardware prefetcher from optimizing data throughput.

The C++ Advantage:

By using a contiguous `std::vector` of POD (Plain Old Data) structs, we achieve linear memory access. This allows the CPU to leverage its L1/L2 caches effectively, eliminates dictionary lookups through fixed memory offsets, and removes the interpreter overhead, resulting in a 10x–100x speedup for metadata-heavy analytics.

```text
PYTHON (hot loop)              C++ (hot loop)
──────────────────────────     ──────────────────────────
list -> pointer                vector -> data
      -> object
         -> dict
            -> value           direct offset load
hash + lookup                  no lookup
pointer chase                  linear memory access
interpreter dispatch           compiled loop
heap objects everywhere        one contiguous buffer
```

```text
# Python
       CPU (The "Hot Loop" Runner)
        │
        │  for obj in objects:  <-- Interpreter bytecode execution
        │
        ▼
┌────────────────────────────┐
│ Python List (Array)        │   ← Contiguous array of 8-byte pointers
│ [ ptr_A ][ ptr_B ][ ptr_C ]│
└────║─────────│─────────│───┘
     ║         │         │
     ▼         ▼         ▼
┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│  PyObject A  │   │  PyObject B  │   │  PyObject C  │  ← Scattered on the HEAP
├──────────────┤   └──────────────┘   └──────────────┘    (Causes Cache Misses!)
│ Ref Count    │                                        ← Metadata overhead
│ Type Pointer │                                        ← "I am a 'Track' object"
│ __dict__ ptr │──┐
└──────────────┘  │
                  ▼
          ┌──────────────┐
          │ Instance Dict│  ← HASH TABLE LOOKUP for "bbox"
          ├──────────────┤    (Expensive CPU work!)
          │ "bbox" : ptr │──┐
          └──────────────┘  │
                            ▼
                    ┌──────────────┐
                    │ PyFloat Obj  │  ← The actual data (cx)
                    ├──────────────┤    (Another heap hop!)
                    │ Value: 12.5  │
                    └──────────────┘
```

```text
# C++
       CPU
        │
        │  for (const auto& obj : objects):
        │
        ▼
┌───────────────────────────────────────────┐
│ std::vector<TrackData> (Contiguous)       │
│ ┌─────────┐┌─────────┐┌─────────┐         │
│ │ Track A ││ Track B ││ Track C │         │
│ │ [bbox]  ││ [bbox]  ││ [bbox]  │         │  ← NO pointers. NO dicts.
│ │ [cx/cy] ││ [cx/cy] ││ [cx/cy] │         │    NO scattered heap.
│ └─────────┘└─────────┘└─────────┘         │
└───────────────────────────────────────────┘
```

## Learning Notes

In-depth write-ups moved out of this README to keep it a navigation hub:

- [docs/memory-hierarchy.md](./docs/memory-hierarchy.md) — the cache "battlefield" diagram and latency chain
- [docs/allocators-cache.md](./docs/allocators-cache.md) — Day 05 allocator & cache lecture
- [docs/jargon.md](./docs/jargon.md) — real-time eng jargon (hot path, false sharing, NUMA, allocator…)

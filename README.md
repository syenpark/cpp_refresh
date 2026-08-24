# C++ Refresh: A Systems-Oriented ML Infrastructure Lab

[![C++ Pre-commit Checks](https://github.com/syenpark/cpp_refresh/actions/workflows/cpp-precommit.yml/badge.svg)](https://github.com/syenpark/cpp_refresh/actions/workflows/cpp-precommit.yml)

A hands-on engineering lab exploring **C++, low-latency data processing, and distributed ML systems**.

Modern ML systems are increasingly limited not only by model inference, but also by everything around the model:

- data movement
- serialization/deserialization
- memory behavior
- CPU efficiency
- process communication
- resource utilization

This project focuses on understanding and optimizing these system-level bottlenecks by building components explicitly from the ground up.

The goal is not to hide complexity behind frameworks, but to build intuition about what happens underneath them.

> [!IMPORTANT]
The [./src/cpp](./src/cpp/) directory contains the C++ analytics bootstrap. It focuses on CMake, ZeroMQ, configuration, JSON decoding, and low-latency systems concepts. For more details, see the [./src/cpp/README.md](./src/cpp/README.md).

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

## Contents

- [Download the Built Images](#download-the-built-images)
- [Create the Linux Systems Lab Cluster](#create-the-linux-systems-lab-cluster-on-mac-m2)
- [Battlefield](#battlefield-)
- [Topics](#topics-)
- [Allocators & Cache Behavior (Day 05)](#allocators--cache-behavior-day-05-)
- [Practical Application: Video Analytics](#practical-application-video-analytics-)

## Battlefield [↑](#contents)

<details>
<summary> Click to expand/collapse </summary>

```shell
                    ┌──────────────────────────────┐
                    │            CPU               │
                    │                              │
                    │  ┌──────── Core 0 ────────┐  │ Core: Executes instructions
                    │  │ Registers (R0..Rn)     │  │    Register: Fastest storage
                    │  │ L1 Cache (32KB)        │  │              Instructions operate
                    │  └────────────────────────┘  │    L1 Cache: Hot variables should live here
                    │                              │
                    │  ┌──────── Core 1 ────────┐  │
                    │  │ Registers (R0..Rn)     │  │
                    │  │ L1 Cache (32KB)        │  │
                    │  └────────────────────────┘  │
                    │                              │
                    │        Shared L2 Cache       │ L2 Cache: Bigger and slower than L1
                    │          (per-core / small)  │
                    │                              │
                    │  ┌────────────────────────┐  │
                    │  │        L3 Cache        │  │ L3 Cache: Shared across cores
                    │  │   (Shared, MBs)        │  │           Bigger and slower than L2
                    │  └────────────────────────┘  │
                    └──────────────┬───────────────┘
                                   │
                        Local Memory Controller
                                   │
                ┌──────────────────┴────────────────────────────────────────────────┐
                │                                                                   │
        RAM (NUMA Node 0)                                                   RAM (NUMA Node 1)
        ~80ns latency                                                        ~150ns latency
    +---------------------------------------------------------------+
    |                   MAIN SYSTEM MEMORY (RAM)                    |
    |         (Shared Address Space for all Cores/Threads)          |
    |                                                               |
    |  +---------------------------------------------------------+  |
    |  | [ STACK ] (Thread 1) | [ STACK ] (Thread 2)             |  |
    |  | (Local variables, function return addresses)            |  |
    |  +---------------------------------------------------------+  |
    |  | [ HEAP ]                                                |  |
    |  | (Dynamically allocated: new / std::shared_ptr)          |  |
    |  +---------------------------------------------------------+  |
    |  | [ DATA SEGMENT ]                                        |  |
    |  | (Globals, static variables, constexpr mutexes)          |  |
    |  +---------------------------------------------------------+  |
    |  | [ CODE SEGMENT ]                                        |  |
    |  | (Your compiled binary / machine instructions)           |  |
    |  +---------------------------------------------------------+  |
    +---------------------------------------------------------------+
```

How latency can grow...

```shell
Instruction →
    uses Registers →
        if miss → L1 →
            miss → L2 →
                miss → L3 →
                    miss → RAM (NUMA local?) →
                        miss → RAM (NUMA remote)
```

</details>

## Topics [↑](#contents)

- [day01](./src/cpp/day01/): stack vs heap
- [day02](./src/cpp/day02/): reference vs copy
- [day03](./src/cpp/day03/): elide vs move vs copy
- [day04](./src/cpp/day04/): STL Containers & API Design
- [day07](./src/cpp/day07/): atomic vs mutex
- [week02](./src/cpp/week02/): memory ordering
- [week03](./src/cpp/week03/): lock-free queue
- [week04](./src/cpp/week04/): micro benchmark

## Allocators & Cache Behavior (Day 05) [↑](#contents)

<details>
<summary> Click to expand/collapse </summary>

### What an allocator actually is

An `allocator` answers two questions:

1. Where do I get memory?
2. How fast and predictable is it?

Default allocators (malloc, new) are:
    - thread-safe (locks)
    - general-purpose
    - optimized for average throughput, not tail latency

Problems:
    - lock contention
    - heap fragmentation
    - unpredictable pauses
    - cache-unfriendly reuse

### The memory hierarchy

```bash
Registers
L1 cache   (~1 ns)
L2 cache   (~4 ns)
L3 cache   (~10–15 ns)
RAM        (~100 ns)
```

One RAM access = hundreds of CPU instructions, so a cache miss hurts more than a copy.

### Cache lines

- Cache lines ~= 64 bytes
- CPU loads entire cache line, not a single variable

If your struct is poorly laid out:
    - You pull in useless data
    - You evict useful data
    - Latency explodes

```cpp
struct Bad {
    bool flag;
    double price;
    bool active;
}
```

```cpp
struct Good {
    double price;
    bool flag;
    bool active;
}
```

Group hot data together.

</details>

## Practical Application: Video Analytics [↑](#contents)

This repository also includes PyTorch distributed-training and M2 GPU timing experiments in [`training/`](./training/). See the [training guide](./training/README.md) for details.

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
2. Dynamic Lookups: Every attribute access (obj.bbox) triggers a hash table lookup in the instance dictionary.
3. Memory Fragmentation: Python objects are scattered across the heap, preventing the CPU hardware prefetcher from optimizing data throughput.

The C++ Advantage:
By using a contiguous std::vector of POD (Plain Old Data) structs, we achieve linear memory access. This allows the CPU to leverage its L1/L2 caches effectively, eliminates dictionary lookups through fixed memory offsets, and removes the interpreter overhead, resulting in a 10x-100x speedup for metadata-heavy analytics.

```bash
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

```bash
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

```bash
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

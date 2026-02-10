# C++ Refresh

I'm currently brushing up on my C++ skills to prepare for a career as a real-time engineer in the age of AI. Check out [jargon.md](./docs/jargon.md) for the fundamental terms you'll need to know.

> [!IMPORTANT]
The [./src/cpp](./src/cpp/) directory contains the C++ analytic container, which serves as the core of this repository, rewriting the original Python implementation for improved performance. For more details, see the [./src/cpp/README.md](./src/cpp/README.md).

## Contents

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
                ┌──────────────────┴──────────────────┐
                │                                     │
        RAM (NUMA Node 0)                     RAM (NUMA Node 1)
        ~80ns latency                         ~150ns latency
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

## Allocators & Cache Behavior (Day 05) [↑](#contents)

<details>
<summary> Click to expand/collapse </summary>

### What an allocator actually is

An `allocator` answers two questons:

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
    - cache-unfriendly resue

### The memory hierarchy

```bash
Registers
L1 cache   (~1 ns)
L2 cache   (~4 ns)
L3 cache   (~10–15 ns)
RAM        (~100 ns)
```

One RAM access = hundres of CPU instructions, so a cache miss hurts more than a copy.

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

Based on my experience, fine-tuning pre-trained AI models for specific applications is becoming increasingly straightforward, thanks to the optimization of inference frameworks, particularly on GPUs.

As model inference becomes faster and more efficient, the true bottleneck often shifts to data flow and real-time decision-making within the Python-based container. Python's inherent inefficiencies in the post-processing layer, especially on hot paths, can significantly hinder performance.

In AI-heavy applications—such as video streaming, autonomous driving, smart cities, and trading—optimizing everything beyond inference is critical. C++ plays a key role in eliminating these inefficiencies and squeezing out those final milliseconds. Therefore, I will simulate the Python hot path for processing object detection metadata and re-implement it in C++ to achieve the performance gains needed for real-time applications.

The directory [./src/python/yolo/inference](./src/python/yolo/inference/) will simulate Ultralytics YOLO's inference and generate dummy metadata, which is used in the analytics layer (In real-world applications, this metadata loop is handled by NVIDIA DeepStream, which is highly optimized).

The Python implementation will serve as a reference for the [analytics](./src/python/yolo/analytics/) pipeline, which I will later re-implement in C++ to optimize the performance of time-critical operations in real-time systems.

```shell
# Inference container is just to simulate metadata generation in quicik.
# In real world applications, I used the highly optimized NVIDIA DeepStream.
+----------------------------+        +----------------------+
|   Inference Container      |        |  Analytics Container | : This is where I will re-write
|  (Dummy Ultralytics YOLO)  |        | (Processing Metadata)|
|                            |        |                      |
|  - Pretend YOLO Inference  |        | - Process Metadata   |
|  - Generate Dummy Metadata |  ----> | - Analytics Logic    |
|                            |        |                      |
+----------------------------+        +----------------------+
```

More details, please check [./src/python/yolo/inference/README.md](./src/python/yolo/inference/README.md) for inference container, [./src/python/yolo/analytics/README.md](./src/python/yolo/analytics/README.md) for analytics container written in Python.

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

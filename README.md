# C++ Refresh

Brush up on C++ personally to be a real-time engineer in AI era. For fundamental jargon you must know, please visit [jargon.md](./docs/jargon.md).

## Battlefield

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

## Topics

[day01](./src/cpp/day01/): stack vs heap
[day02](./src/cpp/day02/): reference vs copy
[day03](./src/cpp/day03/): elide vs move vs copy
[day04](./src/cpp/day04/): STL Containers & API Design

## Allocators & Cache Behavior (Day 05)

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

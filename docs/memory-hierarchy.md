# The Memory Hierarchy

Where instructions, data, and caches live — the "battlefield" you're optimizing against.

```shell
                    ┌──────────────────────────────┐
                    │            CPU               │
                    │                              │
                    │  ┌──────── Core 0 ────────┐  │  Core: Executes instructions
                    │  │ Registers (R0..Rn)     │  │    Register: Fastest storage
                    │  │ L1 Cache (32KB)        │  │              Instructions operate
                    │  └────────────────────────┘  │    L1 Cache: Hot variables should live here
                    │                              │
                    │  ┌──────── Core 1 ────────┐  │
                    │  │ Registers (R0..Rn)     │  │
                    │  │ L1 Cache (32KB)        │  │
                    │  └────────────────────────┘  │
                    │                              │
                    │        Shared L2 Cache       │  L2 Cache: Bigger and slower than L1
                    │          (per-core / small)  │
                    │                              │
                    │  ┌────────────────────────┐  │
                    │  │        L3 Cache        │  │  L3 Cache: Shared across cores
                    │  │   (Shared, MBs)        │  │            Bigger and slower than L2
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

## How latency can grow

```shell
Instruction →
    uses Registers →
        if miss → L1 →
            miss → L2 →
                miss → L3 →
                    miss → RAM (NUMA local?) →
                        miss → RAM (NUMA remote)
```

See also [docs/jargon.md](./jargon.md) for a glossary of cache-miss, false-sharing, NUMA, and allocator terms.

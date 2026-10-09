# Memory Hierarchy & Allocators

Where instructions, data, and caches live — the "battlefield" you're optimizing against.

```shell
                    ┌──────────────────────────────┐
                    │            CPU               │
                    │                              │
                    │  ┌──────── Core 0 ────────┐  │  Core: Executes instructions
                    │  │ Registers (R0..Rn)     │  │    Register: Fastest storage
                    │  │ L1 Cache (32KB)        │  │              Instructions operate
                    │  └────────────────────────┘  │    L1 Cache: Hot variables live here (~1 ns)
                    │                              │
                    │  ┌──────── Core 1 ────────┐  │
                    │  │ Registers (R0..Rn)     │  │
                    │  │ L1 Cache (32KB)        │  │
                    │  └────────────────────────┘  │
                    │                              │
                    │        Shared L2 Cache       │  L2 Cache: bigger/slower than L1 (~4 ns)
                    │          (per-core / small)  │
                    │                              │
                    │  ┌────────────────────────┐  │
                    │  │        L3 Cache        │  │  L3 Cache: shared across cores,
                    │  │   (Shared, MBs)        │  │            bigger/slower than L2 (~10–15 ns)
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

One RAM access costs hundreds of CPU instructions, so a cache miss hurts far
more than an extra copy would.

## Allocators

An allocator answers two questions: *where do I get memory?* and *how fast
and predictable is it?*

Default allocators (`malloc`, `new`) are thread-safe (locked), general-purpose,
and optimized for average throughput rather than tail latency — which shows
up as lock contention, heap fragmentation, unpredictable pauses, and
cache-unfriendly reuse.

## Cache lines

A cache line is ~64 bytes; the CPU loads the whole line, not one variable. A
poorly laid-out struct pulls in useless data, evicts useful data, and makes
latency explode:

```cpp
struct Bad {               struct Good {
    bool flag;                 double price;
    double price;              bool flag;
    bool active;               bool active;
};                          };
```

Group hot data together.

## Case study: pointer-chasing breaks cache locality (Python vs C++)

A Python object list is a worked example of everything above going wrong at
once. Each `obj.bbox` attribute access chases three more pointers (list →
object → `__dict__` → value), and each hop can land on a different,
cold cache line:

```text
# Python: list -> object -> dict -> value, each hop a potential cache miss
┌────────────────────────────┐
│ Python List (Array)        │   ← contiguous array of 8-byte pointers
│ [ ptr_A ][ ptr_B ][ ptr_C ]│
└────║─────────│─────────│───┘
     ▼         ▼         ▼
┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│  PyObject A  │   │  PyObject B  │   │  PyObject C  │  ← scattered on the heap
├──────────────┤   └──────────────┘   └──────────────┘    (cache misses)
│ Ref Count    │
│ Type Pointer │
│ __dict__ ptr │──┐
└──────────────┘  │
                  ▼
          ┌──────────────┐
          │ Instance Dict│  ← hash-table lookup for "bbox" (expensive)
          │ "bbox" : ptr │──┐
          └──────────────┘  │
                            ▼
                    ┌──────────────┐
                    │ PyFloat Obj  │  ← the actual data (another heap hop)
                    │ Value: 12.5  │
                    └──────────────┘

# C++: one contiguous buffer, direct offset load, no pointer chase
┌───────────────────────────────────────────┐
│ std::vector<TrackData> (Contiguous)       │
│ ┌─────────┐┌─────────┐┌─────────┐         │
│ │ Track A ││ Track B ││ Track C │         │  ← no pointers, no dicts,
│ │ [bbox]  ││ [bbox]  ││ [bbox]  │         │    no scattered heap
│ └─────────┘└─────────┘└─────────┘         │
└───────────────────────────────────────────┘
```

A contiguous `std::vector` of POD structs turns that pointer chase into a
linear scan: fixed memory offsets replace dictionary lookups, the CPU's
prefetcher can keep the cache fed, and the interpreter dispatch overhead
disappears entirely — commonly a 10x–100x speedup for metadata-heavy hot
loops like real-time object-detection post-processing.

See also [docs/jargon.md](./jargon.md) for a glossary of cache-miss,
false-sharing, NUMA, and allocator terms.

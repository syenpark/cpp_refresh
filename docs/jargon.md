# Real time engineering Jargon I must know

- `Hot path / hot loop`

    The code that runs all the time, for every event.

    ```cpp
    read_inference_metadata(msg) {
        update_objects_info(msg);
        send_results_to_kafka();
    }
    ```

    That function is hot like receiving object detection metadata in real time for 25 FPS.

- `Cold path`

    Code that runs rarely like startup, confing loading, logging, metrics export.

- `Latency` vs `Throughput`

    How long des one thing take? vs How many things per second?

    `Tail latency`: The slowest cases, not the average.

- `Jitter`

    Random variation in latency, which mostly from OS scheduling, memory allocation, cache misses, NUMA

- `Cache miss`

    CPU wanted data, it wasn't in cache, which could result in CPU stalls and waits on RAM.

    ```shell
        CPU Core
    ┌────────────────────────────┐
    │ Registers                  │  ← fastest
    └──────────┬─────────────────┘
            │
            L1 Cache
    ┌──────────▼─────────────────┐
    │ L1 Cache (per core)        │
    └──────────┬─────────────────┘
            │   ❌ miss
            L2 Cache
    ┌──────────▼─────────────────┐
    │ L2 Cache                   │
    └──────────┬─────────────────┘
            │   ❌ miss
            L3 Cache
    ┌──────────▼─────────────────┐
    │ L3 Cache (shared)          │
    └──────────┬─────────────────┘
            │   ❌ miss
            │
            RAM (NUMA local?)
    ┌──────────▼─────────────────┐
    │ RAM                        │  ← sloooow
    └────────────────────────────┘
    ```

- `False sharing`

    Two cores write different variable on the same cache line, which could lead to cache ping-pong and massive latency spikes.

    ```shell
        Cache Line (64 bytes)
    ┌────────────────────────────────────────────┐
    │ var_A (Core 0) │ var_B (Core 1) │ padding  │
    └────────────────────────────────────────────┘
    ```

    This is harmful, as the below operations repeat.

    ```shell
    Core 0                         Core 1
    ------                         ------
    write var_A
    │
    ▼
    cache line owned by Core 0

                                write var_B
                                    │
                                    ▼
    cache line invalidated on Core 0
                                    │
                                    ▼
    cache line owned by Core 1

    write var_A again
    │
    ▼
    cache line invalidated on Core 1

    Time →
    Core 0: [write][WAIT][write][WAIT][write]
    Core 1: [WAIT][write][WAIT][write][WAIT]
    ```

    How to avoid this;

    ```cpp
    struct alignas(64) OrderBook {
        int price;
        int qty;
    };
    ```

    or explicit padding:

    ```shell
    | var_A | padding (64 bytes) | var_B |
    ```

- `Page fault`

    Memory page not mapped yet, so OS intervention with microseconds to millliseconds delay could occur.

- `Memory locality`

    Data is physically close to the CPU that uses it. Speed levels are register > L1 > L2 > L3 > RAM (local) > RAM (remote NUMA).

- `NUMA`

    Some RAM is farther from your core, so your core accesses RAM that belongs to another CPU socket.

    ```shell
    # NUMA system

    Socket 0                         Socket 1
    ────────                         ────────
    Core 0–15                        Core 16–31
    Local RAM 0                      Local RAM 1
    ```

- `Allocator`

    The system that decides where heap memory comes from.

    ```cpp
    malloc
    new
    tcmalloc
    jemalloc
    ```

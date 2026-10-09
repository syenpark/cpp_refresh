# Real-time Engineering Jargon I Must Know

A short glossary. See [docs/memory-hierarchy.md](./memory-hierarchy.md) for
the cache/RAM diagrams these terms refer to.

- `Hot path` / `Hot loop`

    The code that runs constantly, for every event — e.g. receiving object
    detection metadata in real time at 25 FPS.

    ```cpp
    read_inference_metadata(msg) {
        update_objects_info(msg);
        send_results_to_kafka();
    }
    ```

- `Cold path`

    Code that runs rarely: startup, config loading, logging, metrics export.

- `Latency` vs `Throughput`

    *Latency*: how long does one thing take? *Throughput*: how many things
    per second? `Tail latency` is the slowest cases, not the average.

- `Jitter`

    Random variation in latency, mostly from OS scheduling, memory
    allocation, cache misses, and NUMA.

- `Cache miss`

    The CPU wanted data that wasn't in cache, so it stalls waiting on a
    slower level (L2 → L3 → RAM) — see the latency cascade in
    [memory-hierarchy.md](./memory-hierarchy.md#how-latency-can-grow).

- `False sharing`

    Two cores write different variables that happen to share one cache
    line, causing the line to bounce between cores (cache "ping-pong") and
    latency to spike:

    ```shell
        Cache Line (64 bytes)
    ┌────────────────────────────────────────────┐
    │ var_A (Core 0) │ var_B (Core 1) │ padding  │
    └────────────────────────────────────────────┘

    Core 0: [write][WAIT][write][WAIT][write]
    Core 1: [WAIT][write][WAIT][write][WAIT]
    ```

    Fix with explicit alignment/padding so each variable owns its own line:

    ```cpp
    struct alignas(64) OrderBook {
        int price;
        int qty;
    };
    ```

- `Page fault`

    A memory page isn't mapped yet, so the OS intervenes — microseconds to
    milliseconds of delay.

- `Memory locality`

    Data physically close to the CPU using it is faster: register > L1 > L2
    > L3 > RAM (local) > RAM (remote NUMA).

- `NUMA`

    Some RAM is farther from a given core — it belongs to another CPU
    socket, so accessing it costs more than local RAM. `NUMA miss` is
    walking to another building (remote memory hop); `False sharing` is
    fighting over the same desk (cache-line bouncing):

    | Aspect | False sharing | NUMA miss |
    | --- | --- | --- |
    | RAM accessed | no | yes |
    | Cause | cache line sharing | wrong memory node |
    | Fix | padding / alignment | pin threads + memory |
    | Detectability | very hard | hard |
    | Tail latency | high (p999 spike) | high (random latency) |

- `Allocator`

    The system that decides where heap memory comes from (`malloc`, `new`,
    `tcmalloc`, `jemalloc`) — see
    [memory-hierarchy.md#allocators](./memory-hierarchy.md#allocators).

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

- `False sharing`

- Two cores write different variable on the same cache line, which could lead to cache ping-pong and massive latency spikes.

- `Page fault`

Memory page not mapped yet, so OS intervention with microseconds to millliseconds delay could occur.

- `Memory locality`

Data is physically close to the CPU that uses it. Speed levels are register > L1 > L2 > L3 > RAM (local) > RAM (remote NUMA).

- `NUMA`

Some RAM is farther from your core.

- `Allocator`

The system that decides where heap memory comes from.

```cpp
malloc
new
tcmalloc
jemalloc
```

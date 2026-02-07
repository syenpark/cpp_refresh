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

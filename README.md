# C++ Refresh

Brush up on C++ personally to be a real-time engineer in AI era.

## Topics

[day01](./day01/): stack vs heap  
[day02](./day02/): reference vs copy  
[day03](./day03/): elide vs move vs copy  
[day04](./day04/): STL Containers & API Design

## Allocators & Cache Behavior

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

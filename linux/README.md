# Linux Performance & Troubleshooting

This README is a practical reference for Linux performance concepts useful in ML Systems and ML Infrastructure work.

The emphasis is on troubleshooting mental models, not memorising commands.

## Contents

- [Core mental model](#core-mental-model)
- [Process states](#process-states)
- [Blocking, spinning, and sleeping](#blocking-spinning-and-sleeping)
- [CPU pressure](#cpu-pressure)
- [I/O diagnosis](#io-diagnosis)
- [Process and thread tools](#process-and-thread-tools)
- [Troubleshooting workflow](#troubleshooting-workflow)
- [Commands worth remembering](#commands-worth-remembering)

## Core mental model

When a workload is slow, do not start by assuming the CPU is the problem.

```text
Symptom
  ↓
What resource is limiting progress?
  ↓
CPU / memory / disk I/O / network / synchronization
  ↓
Measure
  ↓
Find the responsible process or thread
  ↓
Identify the bottleneck
  ↓
Mitigate
  ↓
Measure again
```

For PyTorch pipeline flow, DataLoader workers, GPU starvation, synchronization,
and stage timing, see the [Training Lab](../training/README.md). This guide
focuses on the Linux-level signals and tools used to investigate those systems.

## Process states

### Running

A task is executing instructions on a CPU core. Only as many tasks can run
simultaneously as there are available logical CPUs.

### Runnable

A task is ready to run but waiting for CPU scheduling. It is not waiting for
I/O; it simply cannot get CPU time immediately. Sustained runnable pressure is
one signal of CPU contention.

### Blocked / uninterruptible sleep

A task is waiting for an event or resource, commonly disk I/O. It does not
continuously consume a CPU core while waiting, but the application cannot make
progress until the event completes.

### Sleeping

A task voluntarily waits for a condition or time, for example with `sleep()`,
`condition_variable.wait()`, `poll()`, or `select()`.

## Blocking, spinning, and sleeping

### Spinning

A thread repeatedly checks a condition:

```cpp
while (!queue.empty()) {
    // poll for work
}
```

It remains runnable and can consume CPU. Spinning can be useful when the wait
is extremely short, but excessive spinning wastes CPU.

### Blocking

A thread waits for an event or resource and gives up CPU execution while
waiting:

```cpp
condition_variable.wait(lock);
```

A blocking operation can cause a context switch, but blocking and context
switching are not the same thing.

### Sleeping

A sleeping thread does not consume CPU while waiting for time or an event. It
becomes eligible to run again when the sleep expires or it is notified.

| State or behaviour | CPU while waiting? | Typical reason |
| --- | ---: | --- |
| Running | Yes | Executing instructions |
| Runnable | No, waiting for CPU | Scheduling pressure |
| Spinning | Yes | Repeated polling |
| Blocked | No | I/O, resource, or event wait |
| Sleeping | No | Time or event wait |
| Context switch | N/A | CPU changes task |

## CPU pressure

CPU utilisation and CPU saturation are not interchangeable.

On a machine with eight logical CPUs:

```text
us + sy = 20%, r = 2
→ substantial CPU headroom

us + sy = 95%, id = 5%, r = 14
→ sustained CPU contention is likely
```

Do not conclude CPU contention from one `r` value. Look for sustained runnable
pressure together with low idle time.

### Mutex and condition variable

A mutex protects shared state. A condition variable lets a thread wait
without spinning:

```cpp
cv.wait(lock, predicate);
```

```text
queue empty
    ↓
consumer blocks
    ↓
producer adds item
    ↓
notify
    ↓
consumer becomes runnable
```

## I/O diagnosis

### `vmstat`

Use `vmstat` for a broad system-level view:

```bash
vmstat 1
```

| Field | Meaning |
| --- | --- |
| `r` | Runnable tasks |
| `b` | Tasks in uninterruptible sleep |
| `us` | User CPU |
| `sy` | System/kernel CPU |
| `id` | Idle CPU |
| `wa` | I/O wait |
| `in` | Interrupts |
| `cs` | Context switches |
| `bi` | Blocks read |
| `bo` | Blocks written |

Useful first-pass interpretations:

```text
r high + id low
→ CPU pressure

b high + wa high
→ investigate blocked I/O

cs very high
→ investigate scheduling or thread contention
```

`vmstat` provides system-level evidence. It does not identify the responsible
application thread.

### `iostat`

Use `iostat` when the evidence points toward storage:

```bash
iostat -xz 1
```

| Field | Meaning |
| --- | --- |
| `r/s` | Reads per second |
| `w/s` | Writes per second |
| `r_await` | Average read completion latency |
| `w_await` | Average write completion latency |
| `aqu-sz` | Average queue depth |
| `%util` | Device utilisation |

High `await`, queue depth, and device utilisation together strengthen the I/O
bottleneck hypothesis. A high `wa` value alone is a reason to investigate, not
proof of root cause.

The first device table can represent activity accumulated over a longer period,
while later tables represent the requested interval. If a virtual device such
as `vda` is absent from a later table, it usually means there was no qualifying
activity in that interval; the device did not disappear.

### Synthetic I/O results

A command such as:

```bash
dd if=/dev/zero of=/tmp/io-test.bin bs=4M count=512 conv=fdatasync
```

measures one synthetic sequential workload in one environment. It does not
predict random I/O, fsync-heavy workloads, network storage, database access, or
production throughput.

## Process and thread tools

### `top`

Interactive, continuously changing view:

```bash
top
top -H -p <PID>
```

Use it to see current CPU and memory usage, and whether a process's CPU is
distributed across threads or dominated by one thread.

### `ps`

Snapshot and process inventory:

```bash
ps -eo pid,ppid,stat,comm,%cpu --sort=-%cpu
```

Use it for process hierarchy, task state, and current CPU usage.

### `pidstat`

Per-process and per-thread measurements over an interval:

```bash
pidstat -u 1
pidstat -t -p <PID> 1
pidstat -w -p <PID> 1
```

`%CPU` is CPU time consumed during the interval. `CPU` is the logical CPU on
which the task was sampled or accounted. A task can move between logical CPUs.

## Troubleshooting workflow

Use this instead of blindly running commands:

```text
Application is slow
        |
        v
Is CPU saturated?
        |
   +----+----+
   |         |
  YES        NO
   |         |
vmstat       v
r/id     Is there I/O evidence?
             |
          +--+--+
          |     |
         YES    NO
          |     |
       iostat   investigate network,
                memory, sync, or GPU
```

Move from system-level evidence to ownership:

```text
vmstat / iostat
      ↓
which resource?
      ↓
top / ps
      ↓
which process?
      ↓
top -H / pidstat -t
      ↓
which thread?
      ↓
application-level measurement
      ↓
root cause
```

For ML workloads, keep the Linux investigation separate from the framework
stage diagnosis. Once the relevant process or resource is identified, continue
with the [Training Lab troubleshooting map](../training/README.md#training-performance-troubleshooting-map).

## Commands worth remembering

### System overview

```bash
nproc
top
vmstat 1
iostat -xz 1
```

### Process snapshot

```bash
ps -eo pid,ppid,stat,comm,%cpu --sort=-%cpu
```

### Process and thread CPU

```bash
pidstat -u 1
pidstat -t -p <PID> 1
top -H -p <PID>
```

### Context switching

```bash
pidstat -w -p <PID> 1
```

Remember the progression:

```text
vmstat
→ system

top / ps
→ process

top -H / pidstat -t
→ thread

pidstat -w
→ context switching

iostat
→ storage
```

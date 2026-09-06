# Linux Performance & Troubleshooting

This README is a practical reference for Linux performance concepts useful in ML Systems and ML Infrastructure work.

The emphasis is on troubleshooting mental models, not memorising commands.

## Contents

- [Core mental model](#core-mental-model)
- [Process states](#process-states)
- [Blocking, spinning, and sleeping](#blocking-spinning-and-sleeping)
- [CPU pressure](#cpu-pressure)
- [I/O diagnosis](#io-diagnosis)
- [Network diagnosis](#network-diagnosis)
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

A task is waiting in uninterruptible sleep, commonly because the kernel is
waiting for I/O or another low-level operation to complete. It does not
continuously consume CPU while waiting.

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
| Blocked | No | I/O or kernel-level wait |
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
| `r` | Running/runnable tasks |
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
→ investigate further; high context-switch rate alone is not a root cause
```

`vmstat` provides system-level evidence. It does not identify the responsible
application thread.

### `iostat`

Use `iostat` when the evidence points toward storage:

```bash
iostat -xz 1
```

- `-x` = extended device statistics
- `-z` = omit devices with no activity in the interval

| Field | Meaning |
| --- | --- |
| `r/s` | Reads per second |
| `w/s` | Writes per second |
| `r_await` | Average read completion latency |
| `w_await` | Average write completion latency |
| `aqu-sz` | Average outstanding device I/O requests |
| `%util` | Time the device was busy |

`aqu-sz` is a **device I/O queue**, not a CPU scheduler queue. It can contain
both reads and writes.

`vmstat b` and `iostat aqu-sz` do not need to match:

```text
b
→ number of blocked Linux tasks

aqu-sz
→ number of outstanding requests for this block device
```

The relationship is not 1:1. One task can issue multiple asynchronous I/O
requests, and multiple tasks can also wait on shared work or other resources.

High `await`, queue depth, and device utilisation together strengthen the
storage bottleneck hypothesis. A high `wa` value alone is a reason to
investigate, not proof of root cause.

The first device table can represent activity accumulated over a longer period,
while later tables represent the requested interval. If a device is absent
from a later table when `-z` is used, it usually means there was no qualifying
activity during that interval.

### Synthetic I/O results

A command such as:

```bash
dd if=/dev/zero of=/tmp/io-test.bin bs=4M count=512 conv=fdatasync
```

measures one synthetic sequential workload in one environment. It does not
predict random I/O, fsync-heavy workloads, network storage, database access, or
production throughput.

## Network diagnosis

For Linux troubleshooting, this simplified network stack is enough:

```text
Application
docker / curl / Kafka / PyTorch
        |
        v
TCP or UDP          how application data is transported
        |
        v
IP                  where packets are going
        |
        v
Network interface   where packets enter/leave this host
eth0 / lo / veth
        |
        v
Network path / remote host
```

### Network interface

A network interface is the Linux kernel's network endpoint for sending and
receiving packets.

Common examples:

- `eth0` — physical or VM-facing network interface
- `lo` — loopback / localhost
- `veth*` — virtual interface, commonly used by containers
- `cni-podman0` — Podman network bridge

Inspect interfaces and their IP addresses:

```bash
ip addr
```

Inspect routing:

```bash
ip route
```

### IP vs TCP vs UDP

**IP**
- provides addressing and routing
- answers: **where should the packet go?**

**TCP**
- connection-oriented transport over IP
- provides ordered, reliable delivery using ACKs and retransmission
- answers: **how is this reliable connection behaving?**

**UDP**
- datagram transport over IP
- no built-in delivery, ordering, or retransmission guarantee
- useful for workloads where timeliness and low overhead matter

TCP and UDP both operate over IP:

```text
TCP ─┐
     ├── over IP
UDP ─┘
```

### `sar` — interface level

```bash
sar -n DEV 1
```

Useful fields:

| Field | Meaning |
| --- | --- |
| `rxkB/s` | Data received by the interface |
| `txkB/s` | Data transmitted by the interface |
| `%ifutil` | Interface utilisation relative to reported link capacity |

Question answered:

> Is the network interface carrying traffic or close to saturation?

Example:

```text
eth0 rx ≈ 2.5 MB/s
eth0 tx ≈ 40 KB/s
%ifutil ≈ 0.1%
```

Interpretation:

- inbound network traffic exists
- the interface itself is far from saturated
- this does **not** prove the end-to-end network path is healthy

### `ss` — TCP connection level

```bash
ss -ti
```

Use `ss` to inspect TCP connections and TCP-level behaviour such as:

- connection state
- RTT
- retransmission information
- congestion/window behaviour

Question answered:

> Is this TCP connection itself showing signs of delay or loss?

### Example: slow `docker pull`

```text
docker pull slow
        |
        v
vmstat
r low, id high
→ CPU contention unlikely
        |
        v
iostat
await low, aqu-sz low, %util low
→ local storage saturation unlikely
        |
        v
sar -n DEV
RX traffic exists, %ifutil low
→ downloading, but NIC is not saturated
        |
        v
ss -ti
→ inspect the TCP connection
```

Low `%ifutil` does not mean "the network is healthy." A download can still be
slow because of:

- high RTT
- packet loss / retransmissions
- congestion elsewhere on the path
- remote server or registry throttling

Mental model:

```text
sar = interface-level traffic and capacity
ss  = TCP connection-level behaviour
IP  = addressing and routing
TCP/UDP = transport behaviour
interface = packet entry/exit point on this host
```

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

Use it for process hierarchy, task state, command identity, and a snapshot of
CPU usage.

### `pidstat`

Per-process and per-thread measurements over an interval:

```bash
pidstat -u 1
pidstat -u -t -p <PID> 1
pidstat -w -t -p <PID> 1
```

`%CPU` is CPU time consumed during the interval. `CPU` is the logical CPU on
which the task was sampled or accounted. A task can move between logical CPUs.

For context switching:

- `cswch/s` = voluntary context switches per second
- `nvcswch/s` = involuntary context switches per second

High values alone do not prove a problem. Interpret them with runnable pressure,
CPU utilisation, latency, and workload behaviour.

## Troubleshooting workflow

Use this instead of blindly running commands:

```text
Application is slow
        |
        v
vmstat
        |
        +-- r high + id low?
        |       |
        |       └── CPU pressure
        |             ↓
        |          top / ps
        |             ↓
        |          top -H / pidstat
        |
        +-- b/wa high?
        |       |
        |       └── investigate storage
        |             ↓
        |          iostat -xz 1
        |
        +-- neither?
                |
                └── investigate network,
                    memory, sync, GPU,
                    or external dependency
```

For a suspected network issue:

```text
Network suspected
      ↓
sar -n DEV 1
      ↓
interface saturated?
      |
   +--+--+
   |     |
  YES   NO
   |     |
capacity/path   ss -ti
               ↓
          TCP connection?
               ↓
          remote service/path
```

Move from system-level evidence to ownership:

```text
vmstat / iostat / sar
      ↓
which resource or layer?
      ↓
top / ps / ss
      ↓
which process or connection?
      ↓
top -H / pidstat
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
pidstat -u -t -p <PID> 1
top -H -p <PID>
```

### Context switching

```bash
pidstat -w -t -p <PID> 1
```

### Network

```bash
ip addr
ip route
sar -n DEV 1
ss -ti
```

Remember the progression:

```text
vmstat
→ system CPU/task pressure

iostat
→ storage

sar
→ network interface

ss
→ TCP connection

top / ps
→ process

top -H / pidstat -t
→ thread

pidstat -w
→ context switching
```

# Linux Performance & Troubleshooting

This README is a practical reference for Linux performance concepts useful in ML Systems and ML Infrastructure work.

The emphasis is on troubleshooting mental models, not memorising commands.

## Contents

- [Core mental model](#core-mental-model)
- [Process states and waiting](#process-states-and-waiting)
- [CPU pressure](#cpu-pressure)
- [I/O diagnosis](#io-diagnosis)
- [Memory diagnosis](#memory-diagnosis)
- [Network diagnosis](#network-diagnosis)
- [Process and thread tools](#process-and-thread-tools)
- [Tracing and file descriptors](#tracing-and-file-descriptors)
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

## Process states and waiting

The important distinction is:

```text
Can this task run if a CPU becomes available?

YES
├─ Running  → currently executing on a CPU
└─ Runnable → ready to execute, waiting for CPU

NO
├─ S state  → interruptible sleep
└─ D state  → uninterruptible sleep
```

Linux uses TASK_RUNNING for both currently running and runnable tasks.

`vmstat r` therefore reflects running/runnable CPU demand, while `vmstat b`
counts tasks in uninterruptible sleep (D state).

<details>
<summary>Runnable vs D state</summary>

**Runnable**

"I can run now; I only need CPU time."

Typical causes:

- normal computation
- a preempted CPU-bound thread
- a spinning thread waiting for a condition

**D state**

"Giving me CPU would not help yet; I am waiting for a kernel operation to complete."

Common causes:

- storage I/O
- NFS / some filesystem operations
- other kernel-level waits

D does not mean "all blocked threads." It is one specific Linux task state.

</details>

### Blocking is not a Linux task state

"Blocked" is a general programming term meaning:

the thread cannot make progress until some condition, event, or resource changes.

A blocked thread may commonly be:

```text
S state
→ mutex wait
→ condition variable wait
→ sleep / poll / select

D state
→ storage or other uninterruptible kernel wait
```

So:

```text
blocked ≠ D state
vmstat b ≈ D-state tasks
```

### Spinning

Spinning describes behaviour, not a separate scheduler state:

```cpp
while (!ready.load()) {
    // keep checking
}
```

The thread remains eligible to run:

```text
scheduled on CPU → Running
preempted        → Runnable
```

It does not sleep while spinning, so it can consume CPU continuously.

<details>
<summary>Mutex, atomic, and waiting</summary>

mutex and atomic are synchronization mechanisms, not task states.

**Mutex**

If uncontended:

```text
Running
→ acquire lock
→ continue
```

If contended, a typical blocking mutex may:

```text
Running
→ wait / sleep
→ wake
→ Runnable
→ Running
```

A mutex does not necessarily cause a context switch; the uncontended fast path
may complete entirely in user space.

**Atomic operation**

```cpp
counter.fetch_add(1);
```

Usually performs a short atomic operation while the thread is Running.

Atomicity itself does not mean:

- spinning
- sleeping
- blocking
- no context switching

The surrounding algorithm decides the waiting behaviour.

For example:

```cpp
while (!flag.load()) {}
```

is atomic + spinning.

A blocking API such as `atomic::wait()` can instead sleep while waiting.

</details>

### SPSC queue mental model

SPSC means:

- one producer
- one consumer

It does not require lock-free implementation.

```text
SPSC + mutex
→ valid and simple
→ contention may block

SPSC + lock-free atomics
→ avoids mutex ownership contention
→ usually avoids the blocking lock path
→ can reduce latency/jitter
```

Lock-free does not automatically mean "no context switches" or "no waiting."
For example, an empty queue may be handled by spinning, returning immediately,
or a separate blocking mechanism.

### Quick mental map

```text
Running
→ executing now

Runnable
→ CPU-ready, waiting for CPU

S
→ interruptible sleep

D
→ uninterruptible sleep
→ counted by vmstat b

Spinning
→ behaviour while Running/Runnable
→ consumes CPU

Blocking
→ general programming concept
→ commonly S or D

Mutex
→ protects a critical section

Atomic
→ provides atomic operations / synchronization semantics
```

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

### Condition variable

A condition variable lets a thread wait without spinning:

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

<details>
<summary><code>vmstat</code> fields</summary>

| Field | Meaning |
| --- | --- |
| `r` | Running/runnable tasks |
| `b` | Tasks in uninterruptible sleep (`D` state) |
| `us` | User CPU |
| `sy` | System/kernel CPU |
| `id` | Idle CPU |
| `wa` | I/O wait |
| `in` | Interrupts |
| `cs` | Context switches |
| `bi` | Blocks read |
| `bo` | Blocks written |
| `si` | Swap in (from disk) per second |
| `so` | Swap out (to disk) per second |

</details>

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

<details>
<summary><code>iostat</code> fields</summary>

| Field | Meaning |
| --- | --- |
| `r/s` | Reads per second |
| `w/s` | Writes per second |
| `r_await` | Average read completion latency |
| `w_await` | Average write completion latency |
| `aqu-sz` | Average outstanding device I/O requests |
| `%util` | Time the device was busy |

</details>

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

## Memory diagnosis

### `free`

Use `free -h` for a quick memory overview:

```bash
free -h
```

Columns that matter:

- `used` — memory in use
- `buff/cache` — kernel buffers and page cache; reclaimable, not "leaked"
- `available` — estimated free memory for new applications without swapping

A small `free` value next to a large `buff/cache` value is normal: Linux uses
spare memory for the page cache. `available` is the better signal than `free`
for "can this host take more work?"

### Swap

`vmstat` reports swap movement with the `si`/`so` fields in the table above —
pages moving between RAM and disk per second. Swap activity is a symptom, not a
root cause: the memory pressure behind it still needs an explanation.

### OOM

When the kernel cannot reclaim enough memory, it kills a process. Check the
kernel log:

```bash
dmesg | grep -i -E 'out of memory|killed process|oom'
```

The log names the killed process(es) and how much memory was available.

### PSI — pressure stall information

The kernel exposes pressure metrics per resource:

```bash
cat /proc/pressure/cpu
cat /proc/pressure/memory
cat /proc/pressure/io
```

Each file reports `some` and `full` averages over 10s, 60s, and 300s windows.
`some avg10=0.10` means 10% of the last 10 seconds had at least one task
stalled on that resource. `full` close to `some` means the whole machine is
waiting; `full` much lower than `some` means only a few tasks are stalled. PSI
catches memory stalls that are not yet visible as swap or OOM.

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

<details>
<summary><code>sar -n DEV</code> fields</summary>

| Field | Meaning |
| --- | --- |
| `rxkB/s` | Data received by the interface |
| `txkB/s` | Data transmitted by the interface |
| `%ifutil` | Interface utilisation relative to reported link capacity |

</details>

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
ps -ef
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

Also in the lab image and worth having on hand:

- `htop` — interactive `top` with per-thread and tree views
- `pstree` — process / thread hierarchy as a tree
- `fuser -v <path-or-port>` — which PID is using a file or network port
- `ping` — basic reachability; only proves ICMP, not application-layer health

## Tracing and file descriptors

### `strace`

`strace` records the system calls a process makes. Use it when `top -H` and
`pidstat` have identified the thread but not *why* it is stuck:

```bash
strace -f -p <PID>
```

- `-f` — follow forked children
- `-p` — attach to an existing process

For a per-syscall summary instead of a live stream:

```bash
strace -c -p <PID>
```

The summary shows which syscalls dominate, for example repeated `futex`, `poll`,
or `read` calls. It distinguishes "blocked waiting for a lock" from "blocked on
a socket receive" from "doing heavy I/O".

Attaching requires permission; in the lab image that is covered by the
`--cap-add=SYS_PTRACE` flag on the run command in the repository root README.

### `lsof`

`lsof` lists open files — and because "a file" includes sockets, pipes, and
loaded libraries, it shows what a process is holding open:

```bash
lsof -p <PID>
lsof -i :port
lsof +L1
```

- `-p` — files opened by one process
- `-i :port` — processes using a given TCP or UDP port
- `+L1` — files with a link count below 1, i.e. open but already deleted

Open-but-deleted files are the classic "disk is full but nothing seems to own
the space" case: a process keeps a deleted file open, so the space is not
released until that process closes it.

## Troubleshooting workflow

Use this instead of blindly running commands:

### Troubleshooting tree to remember

Start with the broad signal, then narrow down to the responsible process or
thread:

```text
CPU suspected
      vmstat 1          → r high, id low
      top               → identify the process
      pidstat -u        → measure process CPU usage
      pidstat -u -t     → identify the thread

Context-switch suspected
      pidstat -w -t -p <PID> 1
      → compare voluntary and involuntary switches with CPU pressure and latency

I/O suspected
      vmstat 1                  → b high, wa high
      ps -eo pid,stat,wchan,comm → find blocked processes and wait channels
      pidstat -d                → inspect per-process I/O
      iostat -xz 1              → inspect device latency and utilisation

Network suspected
      ss -tuna          → inspect socket states
      sar -n DEV 1      → inspect interface traffic and utilisation
      sar -n TCP 1      → inspect TCP-level errors and retransmissions
```

The signals are clues, not proof: confirm the suspected bottleneck with the
next command in the path and then measure again after mitigation.

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
strace / lsof
      ↓
which syscall? what is held open?
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

### Memory

```bash
free -h
dmesg | grep -i -E 'out of memory|killed process|oom'
cat /proc/pressure/memory
```

### Tracing

```bash
strace -f -p <PID>
strace -c -p <PID>
lsof -p <PID>
lsof -i :port
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

free / dmesg / /proc/pressure/memory
→ memory

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

strace
→ which syscall the thread is stuck on

lsof
→ open files / sockets
```

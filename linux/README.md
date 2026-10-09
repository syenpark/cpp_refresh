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
<summary>Blocking, spinning, mutex/atomic, SPSC, condition variables</summary>

**Runnable vs D state** — Runnable means "I can run now; I only need CPU
time" (normal computation, a preempted CPU-bound thread, a spinning thread).
D means "giving me CPU would not help yet; I am waiting for a kernel
operation" (storage I/O, NFS, other kernel-level waits). D does not mean
"all blocked threads" — it is one specific Linux task state.

**Blocking** is a general programming term — the thread cannot make
progress until some condition, event, or resource changes. A blocked
thread is commonly in S state (mutex wait, condition-variable wait,
sleep/poll/select) or D state (storage/uninterruptible kernel wait). So
`blocked ≠ D state`; `vmstat b` only counts the D-state subset.

**Spinning** describes behaviour, not a separate scheduler state:

```cpp
while (!ready.load()) {
    // keep checking
}
```

The thread stays eligible to run (`scheduled on CPU → Running`,
`preempted → Runnable`) — it does not sleep, so it can consume CPU
continuously.

**Mutex** — if uncontended: `Running → acquire lock → continue`, often
entirely in user space, no context switch. If contended, a typical
blocking mutex does `Running → sleep → wake → Runnable → Running`.

**Atomic** (e.g. `counter.fetch_add(1)`) is usually a short operation while
Running. Atomicity alone does not imply spinning, sleeping, blocking, or a
particular context-switch behaviour — the surrounding algorithm decides
that. `while (!flag.load()) {}` is atomic *and* spinning; a blocking API
such as `atomic::wait()` can instead sleep while waiting.

**SPSC queue** (one producer, one consumer) does not require a lock-free
implementation: `SPSC + mutex` is valid and simple, but contention may
block; `SPSC + lock-free atomics` avoids mutex ownership contention and
usually the blocking path, reducing latency/jitter — but lock-free does
not automatically mean "no waiting": an empty queue may still spin, return
immediately, or fall back to a separate blocking mechanism.

**Condition variable** lets a thread wait without spinning:

```cpp
cv.wait(lock, predicate);
```

```text
queue empty → consumer blocks → producer adds item → notify → consumer runnable
```

</details>

### Quick mental map

```text
Running    → executing now
Runnable   → CPU-ready, waiting for CPU
S          → interruptible sleep
D          → uninterruptible sleep → counted by vmstat b
Spinning   → behaviour while Running/Runnable, consumes CPU
Blocking   → general programming concept, commonly S or D
Mutex      → protects a critical section
Atomic     → atomic operations / synchronization semantics
```

## CPU pressure

CPU utilisation and CPU saturation are not interchangeable.

```text
us + sy = 20%, r = 2                    → substantial CPU headroom
us + sy = 95%, id = 5%, r = 14 (8 cores) → sustained CPU contention is likely
```

Do not conclude CPU contention from one `r` value. Look for sustained
runnable pressure together with low idle time.

### Example: r=12 on 8 cores

```text
vmstat 1  →  r = 12,  b = 0,  wa ≈ 0,  id ≈ 0%   (8 cores)
```

Primary hypothesis: **CPU contention**. `id ≈ 0%` means the CPUs are busy,
and `r = 12 > 8` means runnable demand exceeds available cores. `b = 0` and
`wa ≈ 0` make I/O blocking unlikely.

Then narrow down ownership, one level at a time:

```text
ps -eo pid,ppid,stat,comm,%cpu --sort=-%cpu   → which process?
top -H -p <PID>  or  pidstat -u -t -p <PID> 1 → which thread?
```

If CPU usage is spread evenly across threads, still ask whether it is
expected computation or excessive spinning/contention — 8 threads × 100%
CPU can be either healthy parallel work or threads spinning on a lock.

<details>
<summary>Why does the parent process show 0% CPU?</summary>

Many tools fork workers and let them do the work:

```text
stress (235)   STAT S+   %CPU  0.0   ← forks, then waits
 ├─ stress (236)  STAT R+  %CPU 99.5
 ├─ stress (237)  STAT R+  %CPU 99.4
 ├─ stress (238)  STAT R+  %CPU 99.4
 └─ stress (239)  STAT R+  %CPU 99.5
```

The parent sits in `wait()`, so it consumes almost no CPU. Each process
accounts for its own CPU time; **PPID means "who created me", not "who owns
my CPU usage"**. If you only looked at the parent, you would miss the
problem — that is why the `ps` snapshot lists every process, and
`pstree -p <PID>` shows the hierarchy at a glance.

Note the distinction from threads: here the work is in **child processes**
(visible in plain `ps`), whereas `top -H` / `pidstat -t` descend into
**threads within one process**.

</details>

## I/O diagnosis

### What counts as I/O?

I/O is not just disk reads/writes. It is any operation where the task hands
data to — or waits on — a subsystem other than the CPU:

```text
CPU
├─ storage   (SSD/HDD)   read(file), checkpoint load, log write
├─ network   (NIC)       recv(socket), HTTP request, container image pull
└─ devices   (GPU, USB, sensors)  hardware command completion
```

In Linux performance tooling, "I/O wait" (`wa`) mainly refers to
block device/storage I/O — but network and device waits are still I/O.

### Typical S vs D waits

S/D is about *whether the wait can be interrupted*, not about which I/O type
is involved:

| Situation | Example | Typical state |
| --- | --- | --- |
| time delay | `sleep(10)` | S |
| mutex wait | contended `lock()` | S (usually) |
| condition variable | `cv.wait(lock)` | S |
| socket data | `recv()` with no data yet | S (usually) |
| local disk read | `read()` on block device | D (possible) |
| network filesystem | NFS `read()` waiting on remote server | D (possible) |
| device driver wait | waiting on hardware response | D (possible) |

Troubleshooting order when you find D-state tasks:

```text
D → which kernel wait? (ps wchan, strace)
   → storage / network / device?
   → that layer's tools (iostat, ss, driver logs)
```

### `vmstat`

Use `vmstat` for a broad system-level view:

```bash
vmstat 1
```

`D`, `b`, and `%wa` describe the same I/O-wait phenomenon from three
different viewpoints — none of them is directly interchangeable with
another:

```text
D    (ps, per task)      "what state is this particular process in?"
b    (vmstat, count)     "how many tasks are currently blocked?"
%wa  (vmstat, CPU time)  "how much CPU idle time occurred while I/O was outstanding?"
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
r high + id low   → CPU pressure
b high + wa high  → investigate blocked I/O
cs very high      → investigate further; high rate alone is not a root cause
```

`vmstat` provides system-level evidence. It does not identify the responsible
application thread.

### `iostat`

Use `iostat` when the evidence points toward storage:

```bash
iostat -xz 1   # -x extended stats, -z omit idle devices
```

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

`aqu-sz` is a **device I/O queue**, not a CPU scheduler queue, and it does
not need to match `vmstat b`: one task can issue multiple asynchronous I/O
requests, and multiple tasks can also wait on shared work or other
resources — the relationship is not 1:1.

High `await`, queue depth, and device utilisation together strengthen the
storage-bottleneck hypothesis. A high `wa` value alone is a reason to
investigate, not proof of root cause.

### Synthetic I/O results

```bash
dd if=/dev/zero of=/tmp/io-test.bin bs=4M count=512 conv=fdatasync
```

This measures one synthetic sequential workload in one environment — it does
not predict random I/O, fsync-heavy workloads, network storage, database
access, or production throughput.

## Memory diagnosis

### `free`

```bash
free -h
```

Columns that matter: `used` (memory in use), `buff/cache` (kernel buffers +
page cache — reclaimable, not "leaked"), `available` (estimated free memory
for new applications without swapping). A small `free` value next to a
large `buff/cache` value is normal — Linux uses spare memory for the page
cache. `available` is the better "can this host take more work?" signal.

### Swap

`vmstat`'s `si`/`so` fields report swap movement — pages moving between
RAM and disk per second. Swap activity is a symptom, not a root cause: the
memory pressure behind it still needs an explanation.

### OOM

When the kernel cannot reclaim enough memory, it kills a process:

```bash
dmesg | grep -i -E 'out of memory|killed process|oom'
```

The log names the killed process(es) and how much memory was available.

### PSI — pressure stall information

```bash
cat /proc/pressure/cpu
cat /proc/pressure/memory
cat /proc/pressure/io
```

Each file reports `some` and `full` averages over 10s, 60s, and 300s
windows. `some avg10=0.10` means 10% of the last 10 seconds had at least one
task stalled on that resource. `full` close to `some` means the whole
machine is waiting; `full` much lower than `some` means only a few tasks are
stalled. PSI catches memory stalls that are not yet visible as swap or OOM.

## Network diagnosis

For Linux troubleshooting, this simplified network stack is enough:

```text
Application   docker / curl / Kafka / PyTorch
      ↓
TCP or UDP    how application data is transported
      ↓
IP            where packets are going
      ↓
Interface     where packets enter/leave this host (eth0 / lo / veth)
      ↓
Network path / remote host
```

### Network interface

Common examples: `eth0` (physical/VM-facing), `lo` (loopback), `veth*`
(virtual, commonly used by containers), `cni-podman0` (Podman bridge).

```bash
ip addr     # interfaces and their IP addresses
ip route    # routing
```

### IP vs TCP vs UDP

* **IP** — addressing and routing; answers *where should the packet go?*
* **TCP** — connection-oriented, ordered/reliable delivery via ACKs and
  retransmission; answers *how is this reliable connection behaving?*
* **UDP** — datagram transport, no delivery/ordering/retransmission
  guarantee; useful where timeliness and low overhead matter

Both TCP and UDP operate over IP.

### `sar` — interface level

```bash
sar -n DEV 1
```

<details>
<summary><code>sar -n DEV</code> fields</summary>

| Field | Meaning |
| --- | --- |
| `rxkB/s` | Data received by the interface |
| `txkB/s` | Data transmitted by the interface |
| `%ifutil` | Interface utilisation relative to reported link capacity |

</details>

Question answered: *is the network interface carrying traffic or close to
saturation?*

```text
eth0 rx ≈ 2.5 MB/s, tx ≈ 40 KB/s, %ifutil ≈ 0.1%
→ inbound traffic exists, but the interface itself is far from saturated
→ this does NOT prove the end-to-end network path is healthy
```

### `ss` — TCP connection level

```bash
ss -ti
```

Use `ss` to inspect connection state, RTT, retransmission information, and
congestion/window behaviour. Question answered: *is this TCP connection
itself showing signs of delay or loss?*

### Example: slow `docker pull`

```text
docker pull slow
      ↓
vmstat        r low, id high            → CPU contention unlikely
      ↓
iostat        await/aqu-sz/%util low    → local storage saturation unlikely
      ↓
sar -n DEV    RX traffic exists, %ifutil low  → downloading, NIC not saturated
      ↓
ss -ti        → inspect the TCP connection itself
```

Low `%ifutil` does not mean "the network is healthy" — a download can still
be slow because of high RTT, packet loss/retransmissions, congestion
elsewhere on the path, or remote server/registry throttling.

## Process and thread tools

### `top`

```bash
top
top -H -p <PID>
```

Current CPU/memory usage, and whether a process's CPU is distributed across
threads or dominated by one thread.

### `ps`

```bash
ps -ef
ps -eo pid,ppid,stat,comm,%cpu --sort=-%cpu
```

Process hierarchy, task state, command identity, and a CPU-usage snapshot.

### `pidstat`

```bash
pidstat -u 1
pidstat -u -t -p <PID> 1
pidstat -w -t -p <PID> 1
```

`%CPU` is CPU time consumed during the interval; `CPU` is the logical CPU
the task was sampled/accounted on — a task can move between logical CPUs.
For context switching: `cswch/s` (voluntary) vs `nvcswch/s` (involuntary).
High values alone do not prove a problem — interpret with runnable
pressure, CPU utilisation, latency, and workload behaviour.

Also in the lab image and worth having on hand: `htop` (interactive `top`
with per-thread/tree views), `pstree` (process/thread hierarchy as a tree),
`fuser -v <path-or-port>` (which PID is using a file or network port),
`ping` (basic reachability — only proves ICMP, not application-layer
health).

## Tracing and file descriptors

### `strace`

Use when `top -H`/`pidstat` identified the thread but not *why* it is stuck:

```bash
strace -f -p <PID>    # -f follow forked children, -p attach to existing process
strace -c -p <PID>    # per-syscall summary instead of a live stream
```

The summary shows which syscalls dominate — repeated `futex`, `poll`, or
`read` distinguishes "blocked on a lock" from "blocked on a socket receive"
from "doing heavy I/O". Attaching requires permission; in the lab image
that is covered by `--cap-add=SYS_PTRACE` on the run command in the
repository root README.

### `lsof`

`lsof` lists open files — and because "a file" includes sockets, pipes, and
loaded libraries, it shows what a process is holding open:

```bash
lsof -p <PID>      # files opened by one process
lsof -i :port      # processes using a given TCP/UDP port
lsof +L1           # open but already-deleted files (link count < 1)
```

Open-but-deleted files are the classic "disk is full but nothing seems to
own the space" case: a process keeps a deleted file open, so the space is
not released until that process closes it.

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

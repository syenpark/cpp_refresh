# C++ Lab

Progressive C++ ownership/concurrency topics (`day01` → `week04`/`lab`), plus
a small low-latency analytics bootstrap project.

## Topics

Run each command from inside its directory (`cd day01`, `cd week02`, …).

| Dir | File | Concept | Build & run |
| --- | --- | --- | --- |
| day01 | `lifetime.cpp` | object lifetime — when exactly the destructor runs | `clang++ -std=c++20 -O1 -g -fsanitize=address lifetime.cpp -o lifetime_asan && ./lifetime_asan` |
| day02 | `reference_vs_copy.cpp` | reference vs copy — when a copy actually happens | `clang++ -std=c++20 -O1 -g -fsanitize=address reference_vs_copy.cpp -o reference_vs_copy_asan && ./reference_vs_copy_asan` |
| day03 | `move.cpp` | move semantics — vector buffer transfer vs copy | `clang++ -std=c++20 -O1 -fsanitize=address move.cpp -o move_asan && ./move_asan` |
| day04 | `api.cpp` | vector growth (`reserve`/`emplace_back`), iterator invalidation, ownership patterns (`const&` / `&` / value / `&&`) | `clang++ -std=c++20 -O1 -fsanitize=address api.cpp -o api_asan && ./api_asan` |
| day06 | `numa.cpp` | placeholder — not yet implemented | — |
| day07 | `automic.cpp` | atomic counter: `fetch_add` is atomic, but interleaved `cout` isn't | `clang++ -std=c++20 -O1 -fsanitize=address automic.cpp -o automic_asan && ./automic_asan` |
| day07 | `mutex.cpp` | same counter, mutex-protected (compare against `automic.cpp`) | `clang++ -std=c++20 -O1 -fsanitize=address mutex.cpp -o mutex_asan && ./mutex_asan` |
| week02 | `spsc.cpp` | SPSC lock-free ring buffer, cache-line-padded head/tail | `g++ -O3 -std=c++17 -pthread spsc.cpp && ./a.out` |
| week03 | `treiber.cpp` | Treiber stack — basic lock-free push/pop via CAS | `clang++ -std=c++17 -O0 -pthread treiber.cpp -o treiber && ./treiber` |
| week03 | `treiber_tagged.cpp` | Treiber stack — tagged-pointer CAS to detect ABA on the head | `g++ -std=c++17 -O2 -pthread treiber_tagged.cpp -o treiber_tagged && ./treiber_tagged` |
| week03 | `treiber_hazard.cpp` | Treiber stack — hazard pointers for safe reclamation (fixes use-after-free) | `g++ -std=c++17 -O2 -pthread treiber_hazard.cpp -o treiber_hazard && ./treiber_hazard` |
| week04 | `vector_vs_string.cpp` | `vector<char>` vs `std::string` allocation/append cost | `g++ -std=c++17 -O2 -pthread vector_vs_string.cpp -o vector_vs_string && ./vector_vs_string` |
| lab | `producer_consumer.cpp` | producer-consumer via mutex + condition_variable (also linked into the `analytics` binary below) | `g++ -std=c++17 -O2 -pthread producer_consumer.cpp -o producer_consumer && ./producer_consumer` |

### Atomic (spin) vs Mutex (block) — day07

```text
Thread B waiting on Thread A:

atomic spin:  while (!flag.load()) {}     → uses CPU, no context switch
mutex block:  lock.lock()                 → no CPU, OS wakes it later
```

* spinning avoids context-switch latency but burns CPU — good for short,
  fast-resolving waits (counters, flags)
* blocking frees the CPU but pays a wake-up/context-switch cost — better for
  larger critical sections

## Cheat sheets

### `{}` vs `()` — which one to initialize with?

```cpp
int a(3.7);                   // 3: silently truncates
int b{3.7};                   // compile error: braces forbid narrowing
int c{};                      // 0: value-initialized
int d;                        // garbage (local var): never read before assigning
Widget w();                   // NOT an object: declares a function (most vexing parse)
Widget w{};                   // default-constructed object

std::vector<int> v1(3, 7);    // {7, 7, 7}  → (count, value)
std::vector<int> v2{3, 7};    // {3, 7}     → the list of elements
std::vector<int> v3(5);       // {0,0,0,0,0}
std::vector<int> v4{5};       // {5}

auto x{3};                    // int
auto y = {3};                 // std::initializer_list<int>  (surprise)
```

```text
T x{...}  → "here are the values"       (initializer_list wins if T has one)
T x(...)  → "call this constructor"     (size, count, capacity, …)
T x = v;  → plain copy-init, fine for simple types
```

* Default to `{}`: it zero-initializes and catches narrowing.
* Use `()` when a container should run its count/size constructor (`vector<int>(n)`, `string(n, 'x')`).
  With `{}` you get the elements themselves instead.

### stack vs queue vs deque vs vector — which method name?

```text
vector          [ a b c d ]  ← push_back / pop_back      front()  back()  [i]
                (no push_front: would shift every element)

deque     push_front →  [ a b c d ]  ← push_back          front()  back()  [i]
          pop_front  ←               → pop_back

stack (LIFO)          [ a b c d ]  ← push / pop            top()
                       bottom   top

queue (FIFO)   pop ←  [ a b c d ]  ← push                  front()  back()
                     front   back

priority_queue        heap         push / pop              top()  (largest first)
```

| Container | add | remove | read | `[i]` / iterate |
| --- | --- | --- | --- | --- |
| `vector` | `push_back`, `emplace_back` | `pop_back` | `front`, `back` | yes |
| `deque` | `push_back`, `push_front` | `pop_back`, `pop_front` | `front`, `back` | yes |
| `stack` | `push`, `emplace` | `pop` | `top` | no |
| `queue` | `push`, `emplace` | `pop` | `front`, `back` | no |
| `priority_queue` | `push`, `emplace` | `pop` | `top` | no |

```cpp
std::stack<int> s;
int t = s.top();   // read first...
s.pop();           // ...then remove: pop() returns void
```

* `stack`/`queue`/`priority_queue` are adapters: a restricted interface over a `deque` (or `vector`).
  They name only the end you're allowed to touch: `top` for one end, `push`/`pop` with no `_back`.
* `pop()` returns nothing, and `top()`/`front()`/`pop()` on an empty container is UB. Check `empty()` first.

## Analytics Bootstrap

A C++ bootstrap for a low-latency analytics container: build system, config
loading, and process-level I/O (ZeroMQ) wired up before any analytics hot
path is added.

```text
cpp_refresh/
├── CMakeLists.txt
├── config.toml
├── external/
│   ├── tomlplusplus/   # git submodule (header-only)
│   ├── cppzmq/         # git submodule (header-only)
│   └── rapidjson/      # git submodule (header-only)
├── src/cpp/
│   ├── common/         # config parsing (config.h / config.cpp)
│   ├── analytics/      # main.cpp
│   ├── lab/            # producer_consumer.cpp (see Topics above)
│   └── include/rapidjson.hpp
└── .pre-commit-config.yaml
```

Dependencies: CMake ≥ 3.16, a C++17 compiler, git (for submodules), and
ZeroMQ (libzmq, linked via `pkg-config`; cppzmq and RapidJSON are vendored
as header-only submodules).

```bash
brew install zeromq pkg-config    # macOS

git submodule add https://github.com/marzer/tomlplusplus external/tomlplusplus
git submodule add https://github.com/zeromq/cppzmq external/cppzmq
git submodule add https://github.com/Tencent/rapidjson external/rapidjson
git submodule update --init --recursive
```

`config.toml`:

```toml
[stream]
fps = 0
source_id = 0
uri = "rtsp://camera/stream"
fps_check_interval_sec = 10
max_sources = 1
max_detections = 300

[simulation]
new_object_probability = 0.1
object_exit_probability = 0.05

[zmq]
endpoint = "tcp://127.0.0.1:5555"
socket_type = "sub"
subscribe = "inference"
port = 5555
rcvhwm = 1000
```

Build and run from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_METRICS=ON   # -DENABLE_METRICS=OFF to compile out instrumentation
cmake --build build -j
./build/analytics config.toml
```

Current behavior: parses `config.toml` into a typed `Config` struct, connects
a ZeroMQ SUB socket, receives multipart `(topic, payload)` messages, decodes
the JSON payload (RapidJSON) and iterates per-source/per-detection, and
optionally prints a lightweight FPS when built with `ENABLE_METRICS`. This
repo currently focuses on I/O + decode plumbing — analytics logic comes
later.

Measured under the same input stream: the C++ consumer uses roughly an
order of magnitude less resident memory than an equivalent Python consumer,
and shows lower per-frame CPU cost. Since the pipeline is input-bounded,
FPS alone isn't the metric that matters — CPU cost per frame and memory
headroom under producer pressure are.

Tooling:

```bash
pip install pre-commit && pre-commit install
pre-commit run --all-files
```

Design notes: config is parsed once at startup (struct-based, no hot-path
string lookups); metrics instrumentation is compile-time removable
(`ENABLE_METRICS`); system dependencies like libzmq stay explicit rather
than hidden behind a framework.

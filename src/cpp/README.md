# C++ Analytics Bootstrap (Config + Tooling, ZeroMQ)

This repository is a **C++ bootstrap for a low-latency analytics container**.

The project intentionally starts small and explicit, focusing on:

- build system correctness
- dependency wiring
- configuration loading
- process-level I/O (ZeroMQ)

before introducing any analytics hot paths.

---

## Current Scope

As of now, this repository provides:

- ✅ C++ project skeleton with **CMake**
- ✅ `config.toml` parsing via **toml++**
- ✅ argv-based config path handling
- ✅ clang-format / cpplint / cppcheck wired via **pre-commit**
- ✅ **ZeroMQ** installed and linked (libzmq + cppzmq)
- ✅ JSON parsing via **RapidJSON** (consumer-side decode)
- ✅ Optional compile-time metrics (`ENABLE_METRICS`)
- ❌ No analytics hot loop yet (beyond decode + iteration)
- ❌ No threading / polling / performance tuning yet

The goal is to build this incrementally toward a **low-latency analytics engine**, without hiding system complexity.

---

## Project Structure

```text
cpp_refresh/
├── CMakeLists.txt
├── config.toml
├── external/
│   ├── tomlplusplus/           # git submodule (header-only)
│   ├── cppzmq/                 # git submodule (header-only)
│   └── rapidjson/              # git submodule (header-only)
├── src/
│   └── cpp/
│       ├── common/
│       │   ├── config.h
│       │   └── config.cpp
│       └── analytics/
│           └── main.cpp
├── .pre-commit-config.yaml
└── README.md
```

---

## Dependencies

### Required

- **CMake ≥ 3.16**
- **C++17 compiler** (clang or gcc)
- **git** (for submodules)
- **ZeroMQ** (libzmq)

### Optional (recommended)

- clang-format
- cpplint
- cppcheck
- pre-commit

---

## Installing Dependencies

### toml++

This project uses toml++ (header-only).

```bash
git submodule add https://github.com/marzer/tomlplusplus external/tomlplusplus
git submodule update --init --recursive
```

### ZeroMQ

ZeroMQ consists of:

- **libzmq** (C core, system library)
- **cppzmq** (C++ header-only wrapper)

```bash
# macOS (Homebrew)
brew install zeromq
brew install pkg-config

git submodule add https://github.com/zeromq/cppzmq external/cppzmq
git submodule update --init --recursive
```

### RapidJSON

```bash
git submodule add https://github.com/Tencent/rapidjson external/rapidjson
git submodule update --init --recursive
```

> [!note]
> `libzmq` is treated as a system dependency and linked via `pkg-config`.
> `cppzmq` and `rapidjson` are vendored as header-only submodules.

---

## config.toml

Example:

```toml
[analytics]
max_sources = 4
max_detections = 32

[zmq]
endpoint = "tcp://127.0.0.1:5555"
socket_type = "sub"
subscribe = "inference"
rcvhwm = 1000
```

---

## Build

From the repository root:

```bash
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_METRICS=ON
cmake --build build -j
```

### Disable metrics (compile out instrumentation)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_METRICS=OFF
cmake --build build -j
```

---

## Run

From the repository root:

```bash
./build/analytics config.toml
```

---

## Current Behavior

- Parses `config.toml`
- Loads into typed `Config` struct
- Connects a ZeroMQ SUB socket
- Receives multipart messages: `(topic, payload)`
- Parses JSON payload (RapidJSON) and iterates per-source and per-detection
- Optional: prints lightweight FPS when built with metrics enabled

**Important:** This repo currently focuses on *I/O + decode* plumbing. Analytics logic comes later.

---

## Measurement Notes (Python vs C++)

This repo has been used to compare **Python vs C++ analytics consumers** under the *same input stream*.

Key observation so far:

- **RSS (resident memory)**
  - Python consumer: ~30 MB (example: ~30544 KB)
  - C++ consumer: ~2–3 MB (example: ~2432 KB)

- **CPU (input-limited ~60 FPS)**
  - Both appear low in absolute % because the pipeline is input-bounded.
  - Python still shows higher per-frame CPU cost and less headroom.

Because the pipeline can be input-bounded, **FPS alone is not the primary metric**.
What matters more for scaling is:

- CPU cost per frame
- memory footprint stability
- headroom under increased producer pressure

---

## Tooling

Install hooks:

```bash
pip install pre-commit
pre-commit install
```

Run manually:

```bash
pre-commit run --all-files
```

---

## Design Notes

- Config parsed once at startup
- No hot-path string lookups for config access (struct-based config)
- Metrics instrumentation is compile-time removable (`ENABLE_METRICS`)
- System dependencies (libzmq) stay explicit (not hidden behind a framework)

---

## Next Steps

1. Decode JSON directly into POD structs (avoid dynamic field access in hot paths)
2. Add minimal analytics hot loop (single camera / single ROI)
3. Add controlled load generator (publisher) to push throughput
4. Then: multi-source, ROI fan-out, threading experiments

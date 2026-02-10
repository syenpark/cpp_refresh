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
- ❌ No analytics hot loop yet
- ❌ No threading / polling / performance tuning yet

The goal is to build this incrementally toward a **low-latency analytics engine**, without hiding system complexity.

---

## Project Structure

```text
cpp_refresh/
├── CMakeLists.txt
├── config.toml
├── external/
│   ├── tomlplusplus/          # git submodule (header-only)
│   └── cppzmq/                # git submodule (header-only)
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

- libzmq (C core, system library)
- cppzmq (C++ header-only wrapper)

```bash
brew install zeromq
brew install pkg-config

git submodule add https://github.com/zeromq/cppzmq external/cppzmq
git submodule update --init --recursive
```

> [!note]
libzmq is treated as a system dependency and linked via pkg-config.
cppzmq is vendored as a header-only submodule.

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
subscribe = ""
rcvhwm = 1000
```

---

## Build

From the repository root:

```bash
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
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
- Prints selected values
- Exits

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
- No hot-path string lookups
- Struct-based config enforces clear ownership and lifetime
- System dependencies (libzmq) are kept explicit and visible

This repository intentionally avoids hiding complexity behind frameworks.

---

## Next Steps

1. ZeroMQ init
2. Message recv loop
3. Decode to POD structs
4. Analytics hot loop
5. Metrics / latency instrumentation

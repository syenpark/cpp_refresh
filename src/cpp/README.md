# C++ Analytics Bootstrap (Config + Tooling)

This repository is a **C++ bootstrap for a low-latency analytics container**.

Current scope (as of now):

- ✅ C++ project skeleton with CMake
- ✅ `config.toml` parsing via **toml++**
- ✅ argv-based config path handling
- ✅ clang-format / cpplint / cppcheck wired via pre-commit
- ❌ No ZeroMQ yet
- ❌ No analytics hot loop yet

The goal is to build this incrementally toward a **low-latency analytics engine**.

---

## Project Structure

```text
cpp_refresh/
├── CMakeLists.txt
├── config.toml
├── external/
│   └── tomlplusplus/          # git submodule
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

### Optional (recommended)

- clang-format
- cpplint
- cppcheck
- pre-commit

---

## Installing toml++

This project uses **toml++** (header-only).

```bash
git submodule update --init --recursive
```

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

ZMQ fields are parsed but **not used yet**.

---

## Build

```bash
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

---

## Run

From repo root:

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
- Structured for low-latency evolution

---

## Next Steps

1. ZeroMQ init
2. Message recv loop
3. Decode to POD structs
4. Analytics hot loop
5. Metrics / latency instrumentation

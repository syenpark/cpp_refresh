# A Systems-Oriented Engineer's Lab: C++, Python/PyTorch, Kubernetes

[![C++ Pre-commit Checks](https://github.com/syenpark/cpp_refresh/actions/workflows/cpp-precommit.yml/badge.svg)](https://github.com/syenpark/cpp_refresh/actions/workflows/cpp-precommit.yml)

A hands-on lab across the layers of modern ML infrastructure — low-latency **C++** kernels, **Python/PyTorch** distributed training, and **Kubernetes** orchestration.

Modern ML systems are increasingly limited not only by model inference, but also by everything around the model: data movement, serialization, memory behavior, CPU efficiency, process communication, resource utilization.

From C++ memory management and lock-free queues, to DDP process groups and pod scheduling, this repo builds components explicitly from the ground up. The goal is not to hide complexity behind frameworks, but to build intuition about what happens underneath them.

*Note: the repository retains its original name, `cpp_refresh`, but the content scope is much broader.*

## Contents

- [Repository Layout](#repository-layout)
- [Setup](#setup)
- [Practical Application: Video Analytics](#practical-application-video-analytics)
- [Learning Notes](#learning-notes)

## Repository Layout

| Path | What it is | Guide |
| --- | --- | --- |
| [`src/cpp/`](./src/cpp/) | C++ topics (day01–week04, lab) + analytics bootstrap (CMake, ZeroMQ, config, JSON decode) | [src/cpp/README.md](./src/cpp/README.md) |
| [`training/`](./training/) | PyTorch distributed-training & GPU timing experiments | [training/README.md](./training/README.md) |
| [`k8s/`](./k8s/) | Local Kubernetes lab on kind + Podman: workloads, scheduling, resource quotas | [k8s/README.md](./k8s/README.md) |
| [`linux/`](./linux/) | Linux performance and troubleshooting notes plus an Ubuntu 22.04 systems-tools image | [linux/README.md](./linux/README.md) |
| [`docs/`](./docs/) | Systems notes and learning docs | [memory hierarchy & allocators](./docs/memory-hierarchy.md) · [jargon](./docs/jargon.md) |

## Setup

Python dependencies:

```bash
uv sync
```

Pull and run the lab container images (built by CI — see
[Repository Layout](#repository-layout) above for each lab's own guide):

```bash
echo "$GITHUB_TOKEN" | podman login ghcr.io -u YOUR_GITHUB_USERNAME --password-stdin
podman pull ghcr.io/syenpark/linux-cpp-env:latest
podman pull ghcr.io/syenpark/pytorch-ddp:latest

# Linux Systems Lab, repository mounted at /workspace
podman run --rm -it -v "$PWD:/workspace" --cap-add=SYS_PTRACE ghcr.io/syenpark/linux-cpp-env:latest

# PyTorch DDP, two processes
podman run --rm -it --network host ghcr.io/syenpark/pytorch-ddp:latest \
    torchrun --standalone --nproc-per-node=2 -m training.dataloader_benchmark
```

Kubernetes lab cluster (kind + Podman on macOS):

```bash
brew install kind kubectl
KIND_EXPERIMENTAL_PROVIDER=podman kind create cluster --name mle-lab
kubectl cluster-info --context kind-mle-lab
```

See the [Kubernetes Lab](./k8s/README.md) for the actual exercises.

## Practical Application: Video Analytics

As model inference gets faster, the bottleneck shifts to everything around
it — data flow and real-time decision-making in the post-processing layer.
This repo pairs PyTorch distributed training (see the
[Training Lab](./training/README.md)) with a C++ rewrite of that
post-processing hot path, since Python's per-object pointer-chasing and
dictionary lookups dominate cost at high frame rates. The concrete
before/after — why a contiguous C++ buffer beats scattered Python
objects — is worked through in
[docs/memory-hierarchy.md](./docs/memory-hierarchy.md#case-study-pointer-chasing-breaks-cache-locality-python-vs-c).

## Learning Notes

In-depth write-ups moved out of this README to keep it a navigation hub:

- [docs/memory-hierarchy.md](./docs/memory-hierarchy.md) — the cache "battlefield" diagram, latency chain, allocators, and the Python-vs-C++ pointer-chasing case study
- [docs/jargon.md](./docs/jargon.md) — real-time eng jargon (hot path, false sharing, NUMA, allocator…)

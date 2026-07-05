// treiber_tagged.cpp
// Tagged-pointer Treiber stack (ABA-detecting via (ptr, tag) CAS)
// Build: g++ -std=c++17 -O2 -pthread treiber_tagged.cpp -o treiber_tagged
// Run:   ./treiber_tagged
//
// Notes:
// - This is an *educational* implementation.
// - Tagged pointers help detect ABA on the head pointer.
// - This does NOT solve safe memory reclamation by itself (hazard pointers /
// EBR needed for that).

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

struct Node {
  int value;
  Node *next;
};

// Pack (Node*, tag) into a trivially-copyable struct.
// Many platforms will implement this as a 16-byte atomic CAS (or a lock-based
// fallback).
struct TaggedHead {
  Node *ptr;
  uint64_t tag;
};

static_assert(std::is_trivially_copyable<TaggedHead>::value,
              "TaggedHead must be trivially copyable");

class TreiberTagged {
public:
  TreiberTagged() {
    TaggedHead init{nullptr, 0};
    head_.store(init, std::memory_order_relaxed);
  }

  void push(Node *n) {
    // n must be a valid node; caller controls lifetime (no delete inside).
    TaggedHead old = head_.load(std::memory_order_relaxed);
    for (;;) {
      n->next = old.ptr;

      TaggedHead desired;
      desired.ptr = n;
      desired.tag = old.tag + 1; // bump tag on each head change

      if (head_.compare_exchange_weak(old, desired, std::memory_order_release,
                                      std::memory_order_relaxed)) {
        return;
      }
      // CAS failed: 'old' now holds the current head (ptr,tag). Retry.
    }
  }

  Node *pop() {
    TaggedHead old = head_.load(std::memory_order_acquire);
    while (old.ptr) {
      Node *next = old.ptr->next;

      TaggedHead desired;
      desired.ptr = next;
      desired.tag = old.tag + 1;

      if (head_.compare_exchange_weak(old, desired, std::memory_order_acquire,
                                      std::memory_order_relaxed)) {
        return old.ptr;
      }
      // CAS failed: old updated; loop recomputes next from new old.ptr
    }
    return nullptr;
  }

  // for debugging / stats only
  TaggedHead head_snapshot() const {
    return head_.load(std::memory_order_acquire);
  }

private:
  std::atomic<TaggedHead> head_;
};

// --------- Tiny stress test (no reclamation) ---------
// We pre-allocate nodes and never delete them.
// This avoids use-after-free and focuses on the tagged CAS behavior.

int main() {
  TreiberTagged st;

  constexpr int N = 200000;
  std::vector<Node> nodes;
  nodes.reserve(N);
  for (int i = 0; i < N; ++i) {
    nodes.push_back(Node{i, nullptr});
  }

  // Producer thread pushes all nodes.
  std::thread prod([&] {
    for (int i = 0; i < N; ++i) {
      st.push(&nodes[i]);
    }
  });

  // Consumer thread pops all nodes.
  std::atomic<int> popped{0};
  std::atomic<std::int64_t> value_sum{0};
  std::thread cons([&] {
    int local = 0;
    std::int64_t local_sum = 0;
    while (local < N) {
      const Node *n = st.pop();
      if (!n)
        continue;
      local_sum += n->value;
      ++local;
    }
    popped.store(local, std::memory_order_release);
    value_sum.store(local_sum, std::memory_order_release);
  });

  prod.join();
  cons.join();

  auto h = st.head_snapshot();
  std::cout << "Popped: " << popped.load(std::memory_order_acquire) << "\n";
  std::cout << "Value sum: " << value_sum.load(std::memory_order_acquire)
            << "\n";
  std::cout << "Final head ptr: " << h.ptr << " tag=" << h.tag << "\n";

  // Expect empty stack
  assert(h.ptr == nullptr);
  std::cout << "OK\n";
  return 0;
}

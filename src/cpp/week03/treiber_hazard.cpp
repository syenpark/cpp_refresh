// treiber_hazard.cpp
// Treiber stack with hazard pointers (safe memory reclamation).
//
// Build: g++ -std=c++17 -O2 -pthread treiber_hazard.cpp -o treiber_hazard
// Run:   ./treiber_hazard
//
// Notes:
// - This solves *use-after-free* by deferring deletes until no thread hazards
// the node.
// - This does NOT implement tagged pointers; it focuses on reclamation safety.
// - Works best when you have a small fixed number of threads (typical for
// lock-free HP).

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

struct Node {
  int value;
  Node *next;
};

// ---------------- Hazard Pointers ----------------

constexpr int MAX_THREADS = 8;             // demo limit
std::atomic<void *> g_hazard[MAX_THREADS]; // one hazard slot per thread

// Retired nodes per thread (simple vector). In real systems: linked list +
// batching.
thread_local std::vector<Node *> t_retired;
thread_local int t_tid = -1;

// Set hazard pointer for this thread.
inline void set_hazard(int tid, void *p) {
  g_hazard[tid].store(p, std::memory_order_release);
}

// Clear hazard pointer for this thread.
inline void clear_hazard(int tid) {
  g_hazard[tid].store(nullptr, std::memory_order_release);
}

// Collect all hazard pointers currently published.
static std::vector<void *> collect_hazards() {
  std::vector<void *> hz;
  hz.reserve(MAX_THREADS);
  for (int i = 0; i < MAX_THREADS; ++i) {
    void *p = g_hazard[i].load(std::memory_order_acquire);
    if (p)
      hz.push_back(p);
  }
  return hz;
}

// Scan retired list and free nodes that are not protected.
static void scan_and_reclaim() {
  auto hazards = collect_hazards();
  std::sort(hazards.begin(), hazards.end());

  auto &r = t_retired;
  std::vector<Node *> keep;
  keep.reserve(r.size());

  for (Node *n : r) {
    // if n is in hazard set => keep it (someone might still read it)
    if (std::binary_search(hazards.begin(), hazards.end(),
                           static_cast<void *>(n))) {
      keep.push_back(n);
    } else {
      delete n; // safe to reclaim now
    }
  }
  r.swap(keep);
}

// Retire a node (defer delete) and occasionally scan.
static void retire_node(Node *n) {
  t_retired.push_back(n);
  // Tune threshold for batching. Lower threshold = more scanning overhead.
  if (t_retired.size() >= 64) {
    scan_and_reclaim();
  }
}

// ---------------- Treiber Stack (HP) ----------------

class TreiberHP {
public:
  TreiberHP() : head_(nullptr) {}

  void push(Node *n) {
    Node *old = head_.load(std::memory_order_relaxed);
    do {
      n->next = old;
    } while (!head_.compare_exchange_weak(old, n, std::memory_order_release,
                                          std::memory_order_relaxed));
  }

  Node *pop() {
    assert(t_tid >= 0 && t_tid < MAX_THREADS &&
           "Set t_tid for this thread before using pop()");

    for (;;) {
      Node *old = head_.load(std::memory_order_acquire);
      if (!old) {
        clear_hazard(t_tid);
        return nullptr;
      }

      // Publish hazard: "I am about to dereference old"
      set_hazard(t_tid, old);

      // Re-check that head is still old after publishing hazard.
      // This avoids the window: head changes -> old freed -> we hazard too
      // late.
      if (head_.load(std::memory_order_acquire) != old) {
        continue; // retry: publish hazard for new head
      }

      Node *next = old->next;

      if (head_.compare_exchange_weak(old, next, std::memory_order_acquire,
                                      std::memory_order_relaxed)) {
        // Successfully removed 'old' from stack.
        clear_hazard(t_tid);
        return old; // caller should retire_node(old) after done
      }
      // CAS failed; retry
    }
  }

private:
  std::atomic<Node *> head_;
};

// ---------------- Demo / Stress ----------------

int main() {
  // init hazard slots
  for (int i = 0; i < MAX_THREADS; ++i)
    g_hazard[i].store(nullptr, std::memory_order_relaxed);

  TreiberHP st;

  constexpr int PRODUCERS = 2;
  constexpr int CONSUMERS = 2;
  static_assert(PRODUCERS + CONSUMERS <= MAX_THREADS, "Increase MAX_THREADS");

  constexpr int PER_PRODUCER = 200000;

  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};

  // Producers allocate nodes and push them.
  std::vector<std::thread> threads;

  for (int p = 0; p < PRODUCERS; ++p) {
    threads.emplace_back([&, p] {
      t_tid = p; // 0..PRODUCERS-1
      for (int i = 0; i < PER_PRODUCER; ++i) {
        Node *n = new Node{p * PER_PRODUCER + i, nullptr};
        st.push(n);
        produced.fetch_add(1, std::memory_order_relaxed);
      }
      // No reclamation work needed for producer-only thread
    });
  }

  // Consumers pop nodes and retire them safely.
  for (int c = 0; c < CONSUMERS; ++c) {
    threads.emplace_back([&, c] {
      t_tid = PRODUCERS + c; // next ids

      // Keep consuming until we've consumed everything produced
      // (This is a demo; real systems have shutdown signals.)
      while (consumed.load(std::memory_order_acquire) <
             PRODUCERS * PER_PRODUCER) {
        Node *n = st.pop();
        if (!n)
          continue;

        // Use node data (safe because hazard protected it during pop)
        // After CAS success, no one else can reach 'n' from the stack.
        (void)n->value;

        retire_node(n);
        consumed.fetch_add(1, std::memory_order_relaxed);
      }

      // Final cleanup: reclaim remaining retired nodes.
      scan_and_reclaim();
      // Clear hazard slot already cleared in pop(); just in case:
      clear_hazard(t_tid);
    });
  }

  for (auto &th : threads)
    th.join();

  std::cout << "Produced: " << produced.load() << "\n";
  std::cout << "Consumed: " << consumed.load() << "\n";

  // Reclaim any leftovers in this main thread (if it had any; usually none)
  if (t_tid == -1)
    t_tid = MAX_THREADS - 1;
  scan_and_reclaim();

  std::cout << "OK\n";
  return 0;
}

// clang++ -std=c++17 -O0 -pthread treiber.cpp -o treiber
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

struct Node {
  int value;
  Node* next;
};

class TreiberStack {
 public:
  void push(Node* n) {
    // n must already be allocated by caller
    Node* old = head.load(std::memory_order_relaxed);
    do {
      n->next = old; // publish link to current head
    } while (!head.compare_exchange_weak(old, n, std::memory_order_release,
                                         std::memory_order_relaxed));
  }

  Node* pop() {
    Node* old = head.load(std::memory_order_acquire);
    while (old) {
      Node* next = old->next; // snapshot of next for THIS old
      if (head.compare_exchange_weak(old, next, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
        return old; // caller will free/reuse -> ABA risk
        }
        // CAS failed: `old` updated to current head; loop recomputes `next`
    }
    return nullptr;
  }

  std::atomic<Node*> head{nullptr};
};

// Super dumb "allocator": always returns the same node address after "free".
struct OneSlotPool {
  std::atomic<Node*> slot{nullptr};

  Node* alloc(int v) {
    Node* n = slot.exchange(nullptr, std::memory_order_acq_rel);
    if (!n) n = new Node{};
    n->value = v;
    n->next = nullptr;
    return n;
  }
  void free(Node* n) {
    // overwrite contents to simulate reuse / mutation
    n->value = 999;
    n->next = reinterpret_cast<Node*>(0xDEADBEEF);
    slot.store(n, std::memory_order_release);
  }
};

int main() {
  TreiberStack st;
  OneSlotPool pool;

  // Build stack: A -> B
  Node* A = pool.alloc(1);
  Node* B = pool.alloc(2);
  st.push(B);
  st.push(A);

  // Thread A will try to pop head (expects A)
  std::atomic<bool> t1_read{false};
  std::atomic<bool> t2_done{false};

  Node* t1_old = nullptr;
  Node* t1_next = nullptr;

  std::thread t1([&] {
    // Read head = A, next = B, then pause
    t1_old = st.head.load(std::memory_order_acquire);  // A
    t1_next = t1_old ? t1_old->next : nullptr;         // B
    t1_read.store(true, std::memory_order_release);
    while (!t2_done.load(std::memory_order_acquire)) { /* spin */
    }

    // Now do the CAS using the stale expectation (A -> B)
    // If head == A, CAS succeeds even if A was popped and pushed back.
    Node* expected = t1_old;
    bool ok = st.head.compare_exchange_strong(expected, t1_next,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed);
    std::cout << "T1 CAS ok=" << ok << "\n";
    if (ok) {
      // At this point stack head is set to B, BUT B might not be correct
      // because A may have been reused/mutated.
    }
  });

  std::thread t2([&] {
    while (!t1_read.load(std::memory_order_acquire)) { /* spin */
    }

    // Pop A
    Node* x = st.pop();  // returns A
    assert(x);

    // "Free" A (puts it into pool and mutates it)
    pool.free(x);

    // Re-alloc returns SAME ADDRESS as A
    Node* A2 = pool.alloc(42);  // A2 == A address
    st.push(A2);                // push back at same address -> ABA

    t2_done.store(true, std::memory_order_release);
  });

  t1.join();
  t2.join();

  // The stack may now be corrupted logically (ABA)
  // This demo is intentionally gnarly: it shows CAS can succeed
  // when it shouldn't, because pointer value repeated.
  return 0;
}

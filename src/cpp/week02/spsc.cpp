// g++ -O3 -std=c++17 -pthread spsc.cpp && ./a.out

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

struct SPSC {
  static constexpr size_t N = 8;

  int buffer[N];

  alignas(64) std::atomic<size_t> head{0};
  alignas(64) std::atomic<size_t> tail{0};

  bool push(int v) {
    // 1) load head
    auto h = head.load(std::memory_order_relaxed);
    // 2) compute next
    auto next = (h + 1) & (N - 1);
    // 3) check full using tail
    if (next == tail.load(std::memory_order_acquire)) {
      return false;
    }
    // 4) write buffer
    buffer[h] = v;
    // 5) publish head
    head.store(next, std::memory_order_release);

    return true;
  }

  bool pop(int &out) {
    // 1) load tail
    auto t = tail.load(std::memory_order_relaxed);
    // 2) check empty using head
    if (head.load(std::memory_order_acquire) == t) {
      return false;
    }
    // 3) read buffer
    out = buffer[t];
    // 4) publish tail
    tail.store((t + 1) & (N - 1), std::memory_order_release);
    return true;
  }
};

int main() {
  SPSC q;

  constexpr int ITERS = 100'000'000;

  auto start = std::chrono::high_resolution_clock::now();

  std::thread producer([&]() {
    for (int i = 1; i <= ITERS; ++i) {
      while (!q.push(i)) {
      }
    }
  });

  std::thread consumer([&]() {
    int v;
    for (int i = 1; i <= ITERS; ++i) {
      while (!q.pop(v)) {
      }
    }
  });

  producer.join();
  consumer.join();

  auto end = std::chrono::high_resolution_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                .count();

  std::cout << "Time (ms): " << ms << "\n";
}

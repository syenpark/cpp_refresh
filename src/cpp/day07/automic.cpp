// Compile and run:
// clang++ -std=c++20 -O0 -Wall -Wextra automic.cpp -o automic
// ./automic
// clang++ -std=c++20 -O1 -fsanitize=address automic.cpp -o automic_asan
// ./automic_asan
//
// EXPECTED OUTPUT DIFFERENCES:
// 1. Both threads may see counter value of 2 (not 1 and 2) because:
//    - fetch_add() is atomic ✓
//    - BUT load() is a separate operation that reads AFTER both increments
//
// 2. std::cout output may be INTERLEAVED without AddressSanitizer:
//    - std::cout is NOT thread-safe without mutex protection
//    - Multiple << operations can interleave between threads
//    - ASAN version may appear "correct" only due to timing changes
//      from instrumentation
//
// Key lesson: atomic operations protect the VARIABLE, not std::cout
// or other operations!

#include <atomic>
#include <iostream>
#include <thread>

std::atomic<int> counter(0); // Atomic counter

// Thread A function (uses atomic increment)
void threadA() {
  counter.fetch_add(1, std::memory_order_relaxed);
  // Note: counter.load() is a SEPARATE operation - by the time we
  // read, the other thread may have also incremented, so both might
  // see value 2
  std::cout << "Thread A incremented counter to: " << counter.load() << "\n";
}

// Thread B function (uses atomic increment)
void threadB() {
  counter.fetch_add(1, std::memory_order_relaxed);
  // Same issue: load() happens separately, and std::cout can
  // interleave with Thread A
  std::cout << "Thread B incremented counter to: " << counter.load() << "\n";
}

int main() {
  std::thread tA(threadA);
  std::thread tB(threadB);

  tA.join();
  tB.join();

  return 0;
}

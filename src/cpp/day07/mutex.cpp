// Compile and run:
// clang++ -std=c++20 -O0 -Wall -Wextra mutex.cpp -o mutex
// ./mutex
// clang++ -std=c++20 -O1 -fsanitize=address mutex.cpp -o mutex_asan
// ./mutex_asan

#include <iostream>
#include <mutex>
#include <thread>

int counter = 0; // Shared counter
std::mutex mtx;  // Mutex to protect shared resource

// Thread A function (uses mutex)
void threadA() {
  std::lock_guard<std::mutex> lock(mtx); // Lock mutex
  counter++;                             // Increment the counter
  std::cout << "Thread A incremented counter to: " << counter << "\n";
}

// Thread B function (uses mutex)
void threadB() {
  std::lock_guard<std::mutex> lock(mtx); // Lock mutex
  counter++;                             // Increment the counter
  std::cout << "Thread B incremented counter to: " << counter << "\n";
}

int main() {
  std::thread tA(threadA);
  std::thread tB(threadB);

  tA.join();
  tB.join();

  return 0;
}

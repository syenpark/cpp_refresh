/*
 * Producer-Consumer Example
 * g++ -std=c++17 -O2 -pthread producer_consumer.cpp -o producer_consumer
 */
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

std::queue<int> queue;
std::mutex mutex;
std::condition_variable cv;

bool done = false;

void producer() {
  for (int i = 0; i < 10; ++i) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      queue.push(i);

      std::cout << "produced: " << i << '\n';
    }

    cv.notify_one();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  {
    std::lock_guard<std::mutex> lock(mutex);
    done = true;
  }

  cv.notify_one();
}

void consumer() {
  while (true) {
    std::unique_lock<std::mutex> lock(mutex);

    cv.wait(lock, [] { return !queue.empty() || done; });

    if (queue.empty() && done) {
      break;
    }

    int value = queue.front();
    queue.pop();

    lock.unlock();

    std::cout << "consumed: " << value << '\n';
  }
}

int main() {
  std::thread producer_thread(producer);
  std::thread consumer_thread(consumer);

  producer_thread.join();
  consumer_thread.join();
}

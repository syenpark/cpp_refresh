/* g++ -std=c++17 -O2 -pthread vector_vs_string.cpp -o vector_vs_string
   ./vector_vs_string
*/

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

std::size_t vector_measure(const int &num_elements) {
  std::vector<char> vec;
  vec.reserve(num_elements);

  for (int i = 0; i < num_elements; ++i) {
    vec.push_back('a');
  }

  return vec.size();
}

std::size_t string_measure(const int &num_elements) {
  std::string str;
  str.reserve(num_elements);

  for (int i = 0; i < num_elements; ++i) {
    str += 'a';
  }

  return str.size();
}

int main() {
  const int num_elements = 1000000;
  // Measure time taken to create a vector of integers
  auto start = std::chrono::high_resolution_clock::now();
  const std::size_t vec_size = vector_measure(num_elements);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> vec_duration = end - start;
  std::cout << "Time taken to create vector: " << vec_duration.count()
            << " seconds (size=" << vec_size << ")" << std::endl;

  // Measure time taken to create a string
  start = std::chrono::high_resolution_clock::now();
  const std::size_t str_size = string_measure(num_elements);
  end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> str_duration = end - start;
  std::cout << "Time taken to create string: " << str_duration.count()
            << " seconds (size=" << str_size << ")" << std::endl;

  return 0;
}

/*
clang++ -std=c++20 -O0 -Wall -Wextra move.cpp -o move
clang++ -std=c++20 -O1 -fsanitize=address move.cpp -o move_asan
./move_asan
*/

#include <iostream>
#include <vector>

struct Obj {
    std::vector<int> data;

    // Constructor
    Obj(int n) : data(n) {
        std::cout << "CTOR\n";
    }

    // Copy constructor
    Obj(const Obj& other) : data(other.data) {
        std::cout << "COPY\n";
    }

    // Move constructor
    Obj(Obj&& other) noexcept : data(std::move(other.data)) {
        std::cout << "MOVE\n";
    }

    ~Obj() {
        std::cout << "DTOR\n";
    }
};

// -------- Case A: Guaranteed elision --------
Obj make_elide() {
    Obj x(10);
    return x;  // ELIDE (C++17+)
}

// -------- Case B: Pass by reference (no copy) --------
void read_only(const Obj& o) {
    std::cout << "READ\n";
}

// -------- Case C: Pass by value (copy or move) --------
void take_value(Obj o) {
    std::cout << "VALUE\n";
}

int main() {
    std::cout << "=== A: Temporary initialization ===\n";
    Obj a = Obj(10);   // ELIDE

    std::cout << "\n=== B: Named copy ===\n";
    Obj b = a;         // COPY

    std::cout << "\n=== C: Explicit move ===\n";
    Obj c = std::move(a);  // MOVE

    std::cout << "\n=== D: Return by value ===\n";
    Obj d = make_elide();  // ELIDE

    std::cout << "\n=== E: Pass by reference ===\n";
    read_only(d);          // no copy, no move

    std::cout << "\n=== F: Pass temporary by value ===\n";
    take_value(Obj(5));    // MOVE

    std::cout << "\n=== G: Pass named object by value ===\n";
    take_value(d);         // COPY

    std::cout << "\n=== End of main ===\n";
}
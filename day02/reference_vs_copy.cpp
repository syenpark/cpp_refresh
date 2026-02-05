/*
clang++ -std=c++20 -O0 -Wextra -g reference_vs_copy.cpp -o reference_vs_copy
clang++ -std=c++20 -O1 -g -fsanitize=address reference_vs_copy.cpp -o reference_vs_copy_asan
./reference_vs_copy_asan

*/

#include <iostream>
#include <vector>

struct Obj {
    int id;

    // Constructor
    Obj(int i) : id(i) {
        std::cout << "Construct " << id << "\n";
    }

    // Copy constructor (used when copying an object)
    Obj(const Obj& other) : id(other.id) {
        std::cout << "Copy construct " << id << "\n";
    }

    // Destructor
    ~Obj() {
        std::cout << "Destruct " << id << "\n";
    }
};

// Function that takes an object by reference (no copy)
void f(const Obj& o) {
    std::cout << "In f\n";
}

// Function that takes an object by value (copy happens)
void g(Obj o) {
    std::cout << "In g\n";
}

int main() {
    Obj a(1);       // Object 'a' is created on the stack

    f(a);           // No copy: 'a' is passed by reference to f
    g(a);           // Copy happens: 'a' is copied into 'o' in g

    // At the end of the function, destructors for 'a' and 'o' will be called
}
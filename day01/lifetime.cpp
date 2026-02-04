/*
clang++ -std=c++20 -O0 -Wextra -g lifetime.cpp -o lifetime
clang++ -std=c++20 -O1 -g -fsanitize=address lifetime.cpp -o lifetime_asan
./lifetime_asan

You must be able to answer (out loud)
	•	Why does Destruct 1 happen exactly where it does?
	•	Who decides when Destruct 2 runs?
	•	What wouldn’t call the destructor?

*/

#include <iostream>

struct Obj {
    int id;
    // constructor
    Obj(int i): id(i) { // initializer list vs constructor body assignment
        std::cout << "Construct " << id << "\n";
    }
    // destructor
    ~Obj() {
        std::cout << "Destruct " << id << "\n";
    }
};

void stack_scope() {
    Obj a(1);
}

void heap_scope() {
    Obj* b = new Obj(2); // new always returns a pointer
    delete b;
    // delete doesn't clear the pointer
    // leaving a dangling pointer is dangerous
    b = nullptr;
    // std::cout << "Accessing freed object: " << b->id << "\n";
}

int main() {
    std::cout << "Entering stack_scope\n";
    stack_scope();
    std::cout << "Exited stack_scope\n\n";

    std::cout << "Entering heap_scope\n";
    heap_scope();
    std::cout << "Exited heap_scope\n";
}
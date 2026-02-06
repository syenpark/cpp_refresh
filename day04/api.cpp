/*
clang++ -std=c++20 -O0 -Wall -Wextra api.cpp -o api
./api

clang++ -std=c++20 -O1 -fsanitize=address api.cpp -o api_asan
./api_asan
*/

#include <iostream>
#include <vector>

struct Obj {
    int id;

    Obj(int i) : id(i) {
        std::cout << "CTOR " << id << "\n";
    }

    Obj(const Obj& o) : id(o.id) {
        std::cout << "COPY " << id << "\n";
    }

    Obj(Obj&& o) noexcept : id(o.id) {
        std::cout << "MOVE " << id << "\n";
        o.id = -1; // mark moved-from (debug only)
    }

    ~Obj() {
        std::cout << "DTOR " << id << "\n";
    }
};

// -------- APIs demonstrating ownership --------
void read_only(const Obj& o) {
    std::cout << "READ " << o.id << "\n";
}

void mutate(Obj& o) {
    o.id += 10;
}

void take_by_value(Obj o) {
    std::cout << "TAKE VALUE " << o.id << "\n";
}

void sink(Obj&& o) {
    std::cout << "SINK " << o.id << "\n";
}

// -------- Main --------
int main() {
    std::cout << "\n=== reserve prevents reallocation ===\n";
    std::vector<Obj> v;
    v.reserve(3); // critical

    v.emplace_back(1);   // in-place
    v.emplace_back(2);
    v.emplace_back(3);

    std::cout << "\n=== push_back vs emplace_back ===\n";
    v.push_back(Obj(4)); // temp + MOVE
    v.emplace_back(5);   // direct construct

    std::cout << "\n=== iterator invalidation demo ===\n";
    auto it = v.begin(); // points to v[0]

    std::cout << "Iterator points to: " << it->id << "\n";

    // Uncommenting this may invalidate it if capacity exceeded
    // v.emplace_back(6);

    std::cout << "Iterator still points to: " << it->id << "\n";

    std::cout << "\n=== API ownership patterns ===\n";
    Obj a(100);

    read_only(a);           // const T&
    mutate(a);              // T&
    take_by_value(a);       // COPY
    take_by_value(Obj(200)); // MOVE
    sink(std::move(a));     // explicit consume

    std::cout << "\n=== vector pass by value (ownership) ===\n";
    std::vector<Obj> v2;
    v2.reserve(2);
    v2.emplace_back(10);
    v2.emplace_back(20);

    auto process = [](std::vector<Obj> vec) {
        std::cout << "PROCESS size=" << vec.size() << "\n";
    };

    process(std::move(v2)); // O(1) buffer move

    std::cout << "\n=== End of main ===\n";
}

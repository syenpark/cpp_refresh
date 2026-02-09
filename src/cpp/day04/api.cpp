/*
clang++ -std=c++20 -O0 -Wall -Wextra api.cpp -o api
./api

clang++ -std=c++20 -O1 -fsanitize=address api.cpp -o api_asan
./api_asan
*/

#include <iostream>
#include <utility>
#include <vector>

struct Obj {
  int id;

  Obj(int i) : id(i) { std::cout << "CTOR " << id << "\n"; }

  Obj(const Obj &o) : id(o.id) { std::cout << "COPY " << id << "\n"; }

  Obj(Obj &&o) noexcept : id(o.id) {
    std::cout << "MOVE " << id << "\n";
    o.id = -1; // mark moved-from (debug only)
  }

  ~Obj() { std::cout << "DTOR " << id << "\n"; }
};

// -------- APIs demonstrating ownership --------
void read_only(const Obj &o) { std::cout << "READ " << o.id << "\n"; }

void mutate(Obj &o) { o.id += 10; }

void take_by_value(Obj o) { std::cout << "TAKE VALUE " << o.id << "\n"; }

void sink(Obj &&o) { std::cout << "SINK " << o.id << "\n"; }

// -------- Main --------
int main() {
  std::cout << "\n=== reserve prevents reallocation ===\n";
  std::vector<Obj> v;
  v.reserve(3); // critical

  v.emplace_back(1); // in-place
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

  read_only(a);            // const T&
  mutate(a);               // T&
  take_by_value(a);        // COPY
  take_by_value(Obj(200)); // MOVE
  sink(std::move(a));      // explicit consume

  std::cout << "\n=== vector pass by value (ownership) ===\n";
  std::vector<Obj> v2;
  v2.reserve(2);
  v2.emplace_back(10);
  v2.emplace_back(20);

  auto process = [](std::vector<Obj> vec) { // cppcheck-suppress passedByValue
    std::cout << "PROCESS size=" << vec.size() << "\n";
  };

  process(std::move(v2)); // O(1) buffer move

  std::cout << "\n=== End of main ===\n";
}

/*
═══════════════════════════════════════════════════════════════════════════
PART 1: v.reserve(3) - PRE-ALLOCATION
═══════════════════════════════════════════════════════════════════════════

BEFORE reserve:                      AFTER v.reserve(3):
STACK:                               STACK:
┌──────────────────────────────┐    ┌──────────────────────────────┐
│  vector v:                   │    │  vector v:                   │
│    size: 0                   │    │    size: 0                   │
│    capacity: 0               │    │    capacity: 3               │
│    buffer: nullptr           │    │    buffer → HEAP ───┐        │
└──────────────────────────────┘    └─────────────────────┼────────┘
                                                          │
HEAP:                                HEAP:                │
(nothing allocated)                  ┌────────────────────┼──────────┐
                                     │  ◄─────────────────┘          │
                                     │  [ ??? ][ ??? ][ ??? ]        │
                                     │  └─ 3 slots (uninitialized)   │
                                     └───────────────────────────────┘

Memory allocated but NO objects constructed yet!


═══════════════════════════════════════════════════════════════════════════
PART 2: v.emplace_back(1), v.emplace_back(2), v.emplace_back(3)
═══════════════════════════════════════════════════════════════════════════

After emplace_back(1):               After emplace_back(2):
STACK:                               STACK:
┌──────────────────────────────┐    ┌──────────────────────────────┐
│  vector v:                   │    │  vector v:                   │
│    size: 1                   │    │    size: 2                   │
│    capacity: 3               │    │    capacity: 3               │
│    buffer → HEAP ───┐        │    │    buffer → HEAP ────┐       │
└─────────────────────┼────────┘    └──────────────────────┼───────┘
                      │                                    │
HEAP:                 │             HEAP:                  │
┌─────────────────────┼─────────┐    ┌─────────────────────┼─────────┐
│  ◄──────────────────┘         │    │  ◄──────────────────┘         │
│  [Obj:1][ ??? ][ ??? ]        │    │  [Obj:1][Obj:2][ ??? ]        │
│    ↑ CTOR 1                   │    │           ↑ CTOR 2            │
└───────────────────────────────┘    └───────────────────────────────┘

After emplace_back(3):
STACK:
┌──────────────────────────────┐
│  vector v:                   │
│    size: 3                   │
│    capacity: 3    (FULL!)    │
│    buffer → HEAP ───┐        │
└─────────────────────┼────────┘
                      │
HEAP:                 │
┌─────────────────────┼─────────┐
│  ◄──────────────────┘         │
│  [Obj:1][Obj:2][Obj:3]        │
│                  ↑ CTOR 3     │
└───────────────────────────────┘

Output: "CTOR 1" "CTOR 2" "CTOR 3"
        ↑ Each constructed DIRECTLY in the vector's buffer


═══════════════════════════════════════════════════════════════════════════
PART 3: v.push_back(Obj(4)) - CAPACITY EXCEEDED, REALLOCATION HAPPENS
═══════════════════════════════════════════════════════════════════════════

Step 1: Create temporary Obj(4)
┌──────────────────────────────┐
│  Temporary on stack:         │
│  Obj temp:                   │    Output: "CTOR 4"
│    id = 4                    │
└──────────────────────────────┘

Step 2: Capacity exceeded! Reallocate
OLD HEAP (0x1000):               NEW HEAP (0x2000):
┌──────────────────────────────┐ ┌──────────────────────────────┐
│  [Obj:1][Obj:2][Obj:3]       │ │  [ ??? ][ ??? ][ ??? ]       │
│   (will be moved from)       │ │  [ ??? ][ ??? ][ ??? ]       │
└──────────────────────────────┘ │  └─ Allocated (capacity ~6)  │
                                 └──────────────────────────────┘

Step 3: Move existing objects to new buffer
OLD HEAP:                        NEW HEAP:
┌──────────────────────────────┐ ┌──────────────────────────────┐
│  [moved][moved][moved]       │ │  [Obj:1][Obj:2][Obj:3]       │
│    ↓      ↓      ↓           │ │    ↑      ↑      ↑           │
│  (to be destroyed)           │ │  MOVE 1, MOVE 2, MOVE 3      │
└──────────────────────────────┘ └──────────────────────────────┘

Output: "MOVE 1" "MOVE 2" "MOVE 3"

Step 4: Destroy old objects
OLD HEAP:
┌──────────────────────────────┐
│  [DTOR][DTOR][DTOR]          │
│  └─ Old memory freed         │
└──────────────────────────────┘

Output: "DTOR -1" "DTOR -1" "DTOR -1"  (moved-from objects)

Step 5: Move temporary into new buffer
NEW HEAP:
┌───────────────────────────────┐
│  [Obj:1][Obj:2][Obj:3][Obj:4] │
│                         ↑     │    Output: "MOVE 4"
│                    (from temp)│
└───────────────────────────────┘

Step 6: Destroy temporary
Output: "DTOR -1"  (temporary was moved-from)

FINAL STATE:
STACK:
┌──────────────────────────────┐
│  vector v:                   │
│    size: 4                   │
│    capacity: 6               │
│    buffer → 0x2000 ───┐      │
└───────────────────────┼──────┘
                        │
HEAP (0x2000):          │
┌───────────────────────┼──────┐
│  ◄────────────────────┘      │
│  [Obj:1][Obj:2][Obj:3][Obj:4]│
│  [ ??? ][ ??? ]              │
└──────────────────────────────┘


═══════════════════════════════════════════════════════════════════════════
PART 4: v.emplace_back(5) - WITH AVAILABLE CAPACITY
═══════════════════════════════════════════════════════════════════════════

BEFORE:                          AFTER:
┌──────────────────────────────┐ ┌──────────────────────────────┐
│  vector v:                   │ │  vector v:                   │
│    size: 4                   │ │    size: 5                   │
│    capacity: 6               │ │    capacity: 6               │
│    buffer → HEAP ───┐        │ │    buffer → HEAP ───┐        │
└─────────────────────┼────────┘ └─────────────────────┼────────┘
                      │                                │
HEAP:                 │          HEAP:                 │
┌─────────────────────┼────────┐ ┌─────────────────────┼────────┐
│  ◄──────────────────┘        │ │  ◄──────────────────┘        │
│  [1][2][3][4][ ??? ][ ??? ]  │ │  [1][2][3][4][5][ ??? ]      │
│                              │ │               ↑ CTOR 5       │
└──────────────────────────────┘ │  (constructed in-place!)     │
                                 └──────────────────────────────┘

No reallocation! No moves!
Output: "CTOR 5" only


═══════════════════════════════════════════════════════════════════════════
PART 5: ITERATOR INVALIDATION
═══════════════════════════════════════════════════════════════════════════

auto it = v.begin();
┌──────────────────────────────┐
│  Iterator it:                │
│    ptr → 0x2000 ─────┐       │    Points to v[0]
└──────────────────────┼───────┘
                       │
HEAP (0x2000):         │
┌──────────────────────┼───────┐
│  ◄────────────────────┘      │
│  [Obj:1][Obj:2][Obj:3]...    │
│    ↑                         │
│  it points here              │
└──────────────────────────────┘

Output: "Iterator points to: 1"

IF v.emplace_back(6) were called AND caused reallocation:
┌──────────────────────────────┐
│  Iterator it:                │
│    ptr → 0x2000 ──────X      │    ⚠️ DANGLING!
└──────────────────────────────┘    Old buffer freed!

HEAP (0x2000 - FREED):          HEAP (0x3000 - NEW):
┌──────────────────────────────┐ ┌──────────────────────────────┐
│  ❌ DEALLOCATED MEMORY       │ │  [1][2][3][4][5][6]          │
│  ⚠️ Accessing 'it' = UB      │ │  └─ Vector moved here        │
└──────────────────────────────┘ └──────────────────────────────┘

But since capacity=6 and size=5, one more element fits without reallocation!
So the iterator remains valid.


═══════════════════════════════════════════════════════════════════════════
PART 6: API OWNERSHIP PATTERNS
═══════════════════════════════════════════════════════════════════════════

Obj a(100);                      Output: "CTOR 100"
┌──────────────────────────────┐
│  STACK:                      │
│  Obj a:                      │
│    id = 100                  │
└──────────────────────────────┘


─────────────────────────────────────────────────────────────────────────
read_only(a);  // const Obj& o
─────────────────────────────────────────────────────────────────────────
┌──────────────────────────────┐
│  main():                     │
│    Obj a: id=100 ◄───┐       │    Output: "READ 100"
├──────────────────────┼───────┤
│  read_only():        │       │    ✅ No copy
│    const Obj& o ─────┘       │    ✅ Read-only access
│    (8 byte reference)        │    ✅ Fast
└──────────────────────────────┘


─────────────────────────────────────────────────────────────────────────
mutate(a);  // Obj& o
─────────────────────────────────────────────────────────────────────────
┌──────────────────────────────┐
│  main():                     │
│    Obj a: id=100 ◄───┐       │    After: a.id = 110
├──────────────────────┼───────┤
│  mutate():           │       │    ✅ No copy
│    Obj& o ───────────┘       │    ✅ Can modify original
│    o.id += 10                │    ✅ Fast
└──────────────────────────────┘


─────────────────────────────────────────────────────────────────────────
take_by_value(a);  // Obj o
─────────────────────────────────────────────────────────────────────────
┌──────────────────────────────┐
│  main():                     │    Output: "COPY 110"
│    Obj a: id=110             │           "TAKE VALUE 110"
├──────────────────────────────┤           "DTOR 110"
│  take_by_value():            │
│    Obj o: id=110             │    ❌ Makes copy
│    (separate object)         │    ❌ Slower
│    } → DTOR called           │    ✅ Safe (can't modify original)
└──────────────────────────────┘


─────────────────────────────────────────────────────────────────────────
take_by_value(Obj(200));  // Temporary
─────────────────────────────────────────────────────────────────────────
Step 1: Create temporary
┌──────────────────────────────┐
│  Obj(200) temporary:         │    Output: "CTOR 200"
│    id = 200                  │
└──────────────────────────────┘

Step 2: Move to parameter
┌──────────────────────────────┐
│  take_by_value():            │    Output: "MOVE 200"
│    Obj o: id=200             │           "TAKE VALUE 200"
│    (moved from temp)         │    ✅ Move (not copy)
└──────────────────────────────┘           "DTOR 200"
                                           "DTOR -1" (temp)

─────────────────────────────────────────────────────────────────────────
sink(std::move(a));  // Obj&& o (rvalue reference)
─────────────────────────────────────────────────────────────────────────
BEFORE:                          AFTER:
┌──────────────────────────────┐ ┌──────────────────────────────┐
│  main():                     │ │  main():                     │
│    Obj a: id=110             │ │    Obj a: id=-1              │
│              ↓ move          │ │    (moved-from state)        │
│         (cast to rvalue)     │ │                              │
├──────────────────────────────┤ ├──────────────────────────────┤
│  sink():                     │ │  sink():                     │
│    Obj&& o → refers to a     │ │    Obj&& o                   │
│    o.id is still 110         │ │    Output: "SINK 110"        │
│    (reference, not moved!)   │ │                              │
└──────────────────────────────┘ └──────────────────────────────┘

⚠️ Important: Obj&& is just a reference to an rvalue!
   The move constructor is NOT called.
   The function can read/modify the object, but it's not "moved into" the
function.


═══════════════════════════════════════════════════════════════════════════
PART 7: process(std::move(v2)) - VECTOR MOVE
═══════════════════════════════════════════════════════════════════════════

BEFORE move:
STACK:
┌──────────────────────────────┐
│  main():                     │
│    vector v2:                │
│      size: 2                 │
│      capacity: 2             │
│      buffer → 0x4000 ────┐   │
└──────────────────────────┼───┘
                           │
HEAP (0x4000):             │
┌──────────────────────────┼───┐
│  ◄───────────────────────┘   │
│  [Obj:10][Obj:20]            │
└──────────────────────────────┘

Output: "CTOR 10" "CTOR 20"


DURING process(std::move(v2)):
STACK:
┌──────────────────────────────┐
│  main():                     │
│    vector v2:                │
│      size: 0        ←────┐   │    v2 is now empty!
│      capacity: 0         │   │    (moved-from state)
│      buffer: nullptr     │   │
├──────────────────────────┼───┤
│  process():              │   │
│    vector vec:           │   │
│      size: 2             │   │    Output: "PROCESS size=2"
│      capacity: 2         │   │
│      buffer → 0x4000 ────┼───┤    Ownership transferred!
└──────────────────────────┼───┘
                           │
HEAP (0x4000):             │
┌──────────────────────────┼────┐
│  ◄───────────────────────┘    │
│  [Obj:10][Obj:20]             │
│  └─ SAME buffer (not copied!) │
└───────────────────────────────┘

╔═══════════════════════════════════════════════════════════════╗
║  VECTOR MOVE SEMANTICS                                        ║
║  • Only the control block is copied (~24 bytes)               ║
║  • The heap buffer pointer is transferred                     ║
║  • Source vector left empty (not destroyed)                   ║
║  • O(1) operation - no matter how large the vector!           ║
║  • When vec goes out of scope, it frees the buffer            ║
╚═══════════════════════════════════════════════════════════════╝


AFTER process() returns:
STACK:
┌──────────────────────────────┐
│  main():                     │
│    vector v2:                │
│      (empty)                 │    v2 still exists but empty
└──────────────────────────────┘
                                    Output: "DTOR 10" "DTOR 20"
HEAP:                               (vec's destructor freed buffer)
(buffer 0x4000 freed)


═══════════════════════════════════════════════════════════════════════════
SUMMARY: API PATTERNS
═══════════════════════════════════════════════════════════════════════════

┌──────────────────┬─────────────────┬─────────────────┬──────────────┐
│  Pattern         │  Syntax         │  Copy/Move?     │  Use When    │
├──────────────────┼─────────────────┼─────────────────┼──────────────┤
│ Read-only        │ const T&        │ ❌ Neither      │ Just reading │
│  (borrow)        │                 │ (reference)     │ Don't modify │
│                  │                 │                 │              │
│ Mutable borrow   │ T&              │ ❌ Neither      │ Modify orig  │
│  (in/out param)  │                 │ (reference)     │ No ownership │
│                  │                 │                 │              │
│ Take ownership   │ T               │ ✅ Copy/Move    │ Need own copy│
│  (by value)      │                 │ (lvalue=copy)   │ or consume   │
│                  │                 │ (rvalue=move)   │              │
│                  │                 │                 │              │
│ Consume rvalue   │ T&&             │ ❌ Reference    │ Forward/sink │
│  (rvalue ref)    │                 │ (but can move   │ Perfect fwd  │
│                  │                 │  from it)       │              │
└──────────────────┴─────────────────┴─────────────────┴──────────────┘


PERFORMANCE COMPARISON:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Operation                          Cost
─────────────────────────────────────────────────────────────────
read_only(a)                       O(1) - just pointer
mutate(a)                          O(1) - just pointer
take_by_value(a)                   O(n) - full copy
take_by_value(Obj(200))            O(1) - move (cheap)
sink(std::move(a))                 O(1) - just reference
process(std::move(v2))             O(1) - just pointer steal

Vector reallocation:               O(n) - move all elements
  • With reserve():                Avoided!
  • Without reserve():             Happens multiple times
*/

/*
clang++ -std=c++20 -O0 -Wall -Wextra move.cpp -o move
clang++ -std=c++20 -O1 -fsanitize=address move.cpp -o move_asan
./move_asan
*/

#include <iostream>
#include <utility>
#include <vector>

struct Obj {
  // vector, not a single int anymore
  // vector object on stack, vector's buffer to put valueson heap
  std::vector<int> data;

  // Constructor
  Obj(int n) : data(n) { // Creates vector of size n
    std::cout << "CTOR\n";
  }

  // Copy constructor
  Obj(const Obj &other) : data(other.data) { std::cout << "COPY\n"; }

  // Move constructor
  Obj(Obj &&other) noexcept : data(std::move(other.data)) {
    std::cout << "MOVE\n";
  }

  ~Obj() { std::cout << "DTOR\n"; }
};

// -------- Case A: Guaranteed elision --------
Obj make_elide() {
  Obj x(10);
  return x; // ELIDE (C++17+)
}

// -------- Case B: Pass by reference (no copy) --------
void read_only(const Obj &o) { std::cout << "READ\n"; }

// -------- Case C: Pass by value (copy or move) --------
void take_value(Obj o) { std::cout << "VALUE\n"; }

int main() {
  std::cout << "=== A: Temporary initialization ===\n";
  Obj a = Obj(10); // ELIDE

  std::cout << "\n=== B: Named copy ===\n";
  Obj b = a; // COPY

  std::cout << "\n=== C: Explicit move ===\n";
  Obj c = std::move(a); // MOVE

  std::cout << "\n=== D: Return by value ===\n";
  Obj d = make_elide(); // ELIDE

  std::cout << "\n=== E: Pass by reference ===\n";
  read_only(d); // no copy, no move

  std::cout << "\n=== F: Pass temporary by value ===\n";
  take_value(Obj(5)); // MOVE

  std::cout << "\n=== G: Pass named object by value ===\n";
  take_value(d); // COPY

  std::cout << "\n=== End of main ===\n";
}

/*
═══════════════════════════════════════════════════════════════════════════
CASE A: Obj a = Obj(10) - COPY ELISION
═══════════════════════════════════════════════════════════════════════════

CONCEPTUAL (what it looks like):         ACTUAL (what compiler does):
┌─────────────────────────┐              ┌─────────────────────────┐
│ 1. Create temp Obj(10)  │              │ Construct 'a' directly  │
│ 2. Copy to 'a'          │              │ at final location       │
│ 3. Destroy temp         │              │ (no temporary!)         │
└─────────────────────────┘              └─────────────────────────┘

STACK:                                   Output: "CTOR" (only once)
┌──────────────────────────────┐
│  main() frame                │
│  ┌────────────────────────┐  │         ╔════════════════════════╗
│  │ Obj a:                 │  │         ║ COPY ELISION (C++17+)  ║
│  │   vector data:         │  │         ║ Compiler optimization  ║
│  │   ┌──────────────────┐ │  │         ║ that eliminates        ║
│  │   │ size: 10         │ │  │         ║ temporary objects      ║
│  │   │ capacity: 10     │ │  │         ║ MANDATORY in modern C++║
│  │   │ buffer → HEAP ───┼─┼──┼──┐      ╚════════════════════════╝
│  │   └──────────────────┘ │  │  │
│  └────────────────────────┘  │  │
└──────────────────────────────┘  │
                                  │
HEAP:                             │
┌──────────────────────────────┐  │
│  ◄──────────────────────────────┘
│  [0][0][0][0][0][0][0][0][0][0]  (10 ints)
│  └─ a's data buffer          │
└──────────────────────────────┘


═══════════════════════════════════════════════════════════════════════════
CASE B: Obj b = a - COPY CONSTRUCTION
═══════════════════════════════════════════════════════════════════════════

BEFORE:                              AFTER:
STACK:                               STACK:
┌──────────────────────────────┐    ┌──────────────────────────────┐
│  Obj a:                      │    │  Obj a:                      │
│    data → HEAP(A) ───┐       │    │    data → HEAP(A) ─────┐     │
└──────────────────────┼───────┘    │                        │     │
                       │            │  Obj b:                │     │
HEAP:                  │            │    data → HEAP(B) ─────┼───┐ │
┌──────────────────────┼────────┐   └────────────────────────┼───┼─┘
│  (A) ◄────────────────┘       │                            │   │
│  [0][0][0][0][0][0][0][0][0][0]   HEAP:                    │   │
│                               │    ┌───────────────────────┼───┼────┐
└───────────────────────────────┘    │  (A) ◄────────────────┘   │    │
                                     │  [0][0][0][0][0][0][0][0][0][0]
                                     │                           │    │
                                     │  (B) ◄────────────────────┘    │
                                     │  [0][0][0][0][0][0][0][0][0][0]
                                     │  └─ NEW allocation (EXPENSIVE!)│
                                     └────────────────────────────────┘

Output: "COPY"

╔═══════════════════════════════════════════════════════════════╗
║  COPY SEMANTICS - DEEP COPY                                   ║
║  • Creates NEW vector on heap                                 ║
║  • Copies ALL 10 integers                                     ║
║  • Both objects own their own data                            ║
║  • 'a' and 'b' are completely independent                     ║
║  • EXPENSIVE: O(n) time and memory                            ║
╚═══════════════════════════════════════════════════════════════╝


═══════════════════════════════════════════════════════════════════════════
CASE C: Obj c = std::move(a) - MOVE CONSTRUCTION
═══════════════════════════════════════════════════════════════════════════

BEFORE:                              AFTER:
STACK:                               STACK:
┌──────────────────────────────┐    ┌──────────────────────────────┐
│  Obj a:                      │    │  Obj a:                      │
│    data → HEAP(A) ───┐       │    │    data → nullptr (MOVED!)   │
│           size: 10   │       │    │           size: 0            │
│           capacity: 10       │    │           capacity: 0        │
└──────────────────────┼───────┘    │                              │
                       │            │  Obj c:                      │
HEAP:                  │            │    data → HEAP(A) ───┐       │
┌──────────────────────┼────────┐   │           size: 10   │       │
│  (A) ◄───────────────┘        │   │           capacity: 10       │
│  [0][0][0][0][0][0][0][0][0][0]   └──────────────────────┼───────┘
│                               │                          │
└───────────────────────────────┘    HEAP:                 │
                                     ┌─────────────────────┼─────────┐
                                     │  (A) ◄──────────────┘         │
std::move(a) casts 'a' to rvalue     │  [0][0][0][0][0][0][0][0][0][0]
      ↓                              │  └─ SAME buffer (not copied!) │
Tells compiler: "I don't need        │     Ownership transferred     │
'a' anymore, steal its resources"    └───────────────────────────────┘

Output: "MOVE"

╔═══════════════════════════════════════════════════════════════╗
║  MOVE SEMANTICS - OWNERSHIP TRANSFER                          ║
║  • NO new allocation on heap                                  ║
║  • Just steals pointer from 'a'                               ║
║  • 'a' left in valid but empty state                          ║
║  • 'c' now owns the data                                      ║
║  • CHEAP: O(1) time, just pointer copy                        ║
║  • ⚠️ Don't use 'a' after move (undefined state)              ║
╚═══════════════════════════════════════════════════════════════╝


═══════════════════════════════════════════════════════════════════════════
CASE D: Obj d = make_elide() - RETURN VALUE OPTIMIZATION
═══════════════════════════════════════════════════════════════════════════

Inside make_elide():                 After return:
┌──────────────────────────────┐    ┌──────────────────────────────┐
│  make_elide() frame          │    │  main() frame                │
│  ┌────────────────────────┐  │    │  ┌────────────────────────┐  │
│  │ Obj x(10)              │  │    │  │ Obj d:                 │  │
│  │   Built directly in    │  │    │  │   (same object as x!)  │  │
│  │   caller's 'd' slot    │  │    │  │   data → HEAP ───┐     │  │
│  └────────────────────────┘  │    │  └──────────────────┼─────┘  │
└──────────────────────────────┘    └─────────────────────┼────────┘
        ↓ return x                                        │
   NO COPY, NO MOVE!                   HEAP:              │
   Compiler builds 'x' where           ┌──────────────────┼────────────┐
   'd' will live in main()             │  ◄───────────────┘            │
                                       │  [0][0][0][0][0][0][0][0][0][0]
Output: "CTOR" (only one!)             └───────────────────────────────┘


═══════════════════════════════════════════════════════════════════════════
CASE E: read_only(d) - PASS BY REFERENCE
═══════════════════════════════════════════════════════════════════════════

STACK:
┌──────────────────────────────┐
│  main() frame                │
│  ┌────────────────────────┐  │
│  │ Obj d:                 │  │  ← Original object
│  │   data → HEAP ───┐     │  │
│  └──────────────────┼─────┘  │
├─────────────────────┼────────┤
│  read_only() frame  │        │
│  ┌──────────────────┼─────┐  │
│  │ const Obj& o ────┘     │  │  ← Just a reference (alias)
│  │   (8 byte pointer)     │  │     Points to same object
│  └────────────────────────┘  │     NO construction!
└──────────────────────────────┘

Output: "READ" (no COPY, no MOVE)


═══════════════════════════════════════════════════════════════════════════
CASE F: take_value(Obj(5)) - TEMPORARY MOVED
═══════════════════════════════════════════════════════════════════════════

Step 1: Create temporary
┌──────────────────────────────┐
│  Obj(5) temporary (rvalue)   │    Output: "CTOR"
│    data → HEAP(T) ───┐       │
└──────────────────────┼───────┘
                       │
Step 2: Move to parameter
┌──────────────────────┼───────┐
│  take_value() frame  │       │
│  ┌───────────────────┼─────┐ │    Output: "MOVE"
│  │ Obj o:            │     │ │    (temporary is rvalue,
│  │   data → HEAP(T)──┘     │ │     so move constructor)
│  │   (stolen from temp)    │ │
│  └─────────────────────────┘ │
└──────────────────────────────┘

Step 3: Temporary destroyed
(but it's already empty, so cheap)


═══════════════════════════════════════════════════════════════════════════
CASE G: take_value(d) - NAMED OBJECT COPIED
═══════════════════════════════════════════════════════════════════════════

┌──────────────────────────────┐
│  main() frame                │
│  ┌────────────────────────┐  │
│  │ Obj d:                 │  │    'd' is an lvalue
│  │   data → HEAP(D) ───┐  │  │    (named, can't steal from it
│  └─────────────────────┼──┘  │     without std::move)
├────────────────────────┼─────┤
│  take_value() frame    │     │
│  ┌─────────────────────┼───┐ │    Output: "COPY"
│  │ Obj o:              │   │ │    (must make full copy)
│  │   data → HEAP(O) ───┼─┐ │ │
│  └─────────────────────┼─┼─┘ │
└────────────────────────┼─┼───┘
                         │ │
HEAP:                    │ │
┌────────────────────────┼─┼──────┐
│  (D) ◄─────────────────┘ │      │
│  [0][0][0][0][0][0][0][0][0][0] |
│                           │     │
│  (O) ◄────────────────────┘     │
│  [0][0][0][0][0][0][0][0][0][0] |
│  └─ New allocation (expensive!) │
└─────────────────────────────────┘


═══════════════════════════════════════════════════════════════════════════
COMPARISON: COPY vs MOVE
═══════════════════════════════════════════════════════════════════════════

COPY CONSTRUCTOR:                   MOVE CONSTRUCTOR:
─────────────────────────────       ─────────────────────────────
Source:  [ptr → [data]]             Source:  [ptr → [data]]
           │                                   │
           │ Copy all data                     │ Steal pointer
           ↓                                   ↓
Target:  [ptr → [data]]             Target:  [ptr → [data]]

Source remains valid                Source: [nullptr] (empty)
Both own their data                 Target owns data
Expensive: O(n)                     Cheap: O(1)


┌────────────────┬──────────────────────┬──────────────────────┐
│  Operation     │  What Happens        │  Output              │
├────────────────┼──────────────────────┼──────────────────────┤
│ Obj a=Obj(10)  │  Copy elision        │  CTOR                │
│ Obj b = a      │  Deep copy           │  COPY                │
│ Obj c=move(a)  │  Steal resources     │  MOVE                │
│ return value   │  Copy elision        │  (no extra ops)      │
│ f(const Obj&)  │  Reference (alias)   │  (nothing)           │
│ f(Obj(5))      │  Move temp to param  │  CTOR, MOVE          │
│ f(named_obj)   │  Copy to param       │  COPY                │
└────────────────┴──────────────────────┴──────────────────────┘


KEY RULES:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

1. LVALUE (named object):
   • Obj b = a;           → COPY (must preserve original)
   • Obj c = std::move(a);→ MOVE (explicitly allow stealing)

2. RVALUE (temporary):
   • Obj a = Obj(10);     → ELIDE (no temp created)
   • take_value(Obj(5));  → MOVE (temp is disposable)

3. REFERENCE:
   • void f(const Obj& o);→ No construction at all
   • Just an alias to existing object

4. AFTER MOVE:
   • Object is in "valid but unspecified state"
   • Don't use it except to assign new value or destroy
   • The vector is left empty (size=0, ptr=nullptr)
*/

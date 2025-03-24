#include <cilk/cilk.h>
#include <cstdlib>
#include <iostream>

int constexpr size = 1 << 10;

__attribute__((noinline))
void f(int* ptr) {
  free(ptr); 
  for (int i = 0; i < 1000; i++)
    i = i;
  std::cout << "fffffffffffffffff" << std::endl;
}

__attribute__((noinline))
void g(int * ptr) {
  ptr[size/2] = 7;
  for (int i = 0; i < 1000; i++)
    i = i;
  std::cout << "ggggggggggggggggg" << std::endl;
}

__attribute__((noinline))
void stuff() {
  int* arr = (int*)malloc(sizeof(int) * size);
  arr[2] = 91;
  free(arr);
}

__attribute__((noinline))
void f_stuff(int* ptr) {
  f(ptr);
  stuff();
}

__attribute__((noinline))
void g_stuff(int* ptr) {
  g(ptr);
  stuff();
}

__attribute__((noinline))
void parallel_malloc(int*& ptr) {
  ptr = (int*)malloc(sizeof(int) * size);
}

__attribute__((noinline))
void parallel_free(int* ptr) {
  free(ptr);
}

/* 3 cases of malloc free to the same address
Case 0: Free before Malloc.
  User sent information back in time. [Won't fix]
  
Case 1: Free in Parallel with Malloc.
  User had to coordinate the address across parallel sections.
  Therefore, we can detect this with reads and writes. 
  Malloc and free aren't special here; they're the second race.
  Or, the malloc and free are incidentally the same address and don't form a pair.

Case 2:
  Malloc happens-before free (in every execution). 
  Corresponds to case 1 below

*/

/* Thoughts:
You can always determine which malloc pairs with a free. Most recent ancestor that malloc'd that address.
Therefore, you can determine what writes are associated with a malloc.
Can we solve this by tagging writes with their associated malloc?
Frees should NOT destroy malloc tagging information.
We don't need to store the entire malloc-dag, just ancestral information?
I'm really compelled by ancestral malloc.
*/

int main() {

// CASE 0: Race between malloc and free
// Lesson: Is asan a good base for assumption? Not really.
  int* arr;
  if (false) {
    cilk_spawn parallel_malloc(arr);
    cilk_spawn parallel_free(arr);
    cilk_sync;
    return 0;
  }

  // CASE 1: Race between free and write
  // Lesson: Frees count as writes
  // Lesson2: Cilksan doesn't catch this >:3
  // Lesson3: asan doesn't catch this >>:33 (it does if you swap f and g's call order)
  if (false) {
    arr = (int*)malloc(sizeof(int) * size);
    cilk_spawn g(arr);
    cilk_spawn f(arr);
    cilk_sync;
    return 0;
  }
  // CASE 2: Non-race between two mallocs that return the same
  // Lesson: Eventually clear memory accesses of freed memory, because frees can happen in parallel with mallocs
  if (false) {
    cilk_spawn stuff();
    cilk_spawn stuff();
    cilk_sync;
    return 0;
  }

  // CASE 3: Race between free and write hidden by mallocs
  // Lesson: :(
  if (false) {
    arr = (int*)malloc(sizeof(int) * size);
    cilk_spawn g_stuff(arr);
    cilk_spawn f_stuff(arr);
    cilk_sync;
  }
}

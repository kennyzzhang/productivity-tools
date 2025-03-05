#include <cilk/cilk.h>
#include <cstdlib>
#include <iostream>

int constexpr size = 1 << 10;

__attribute__((noinline))
void f(int* ptr) {
  free(ptr); 
  for (int i = 0; i < 1000; i++)
    i = i;
}

__attribute__((noinline))
void g(int * ptr) {
  ptr[size/2] = 7;
  for (int i = 0; i < 1000; i++)
    i = i;
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

int main() {

// CASE 1: Race between free and write
// Lesson: Frees count as writes
// Lesson2: Cilksan doesn't catch this >:3
  int* arr = (int*)malloc(sizeof(int) * size);
  cilk_spawn f(arr);
  cilk_spawn g(arr);
  cilk_sync;

// CASE 2: Non-race between two mallocs that return the same
// Lesson: Eventually clear memory accesses of freed memory, because frees can happen in parallel with mallocs
  cilk_spawn stuff();
  cilk_spawn stuff();
  cilk_sync;

// CASE 3: Race between free and write hidden by mallocs
// Lesson: :(
  arr = (int*)malloc(sizeof(int) * size);
  cilk_spawn f_stuff(arr);
  cilk_spawn g_stuff(arr);
  cilk_sync;


}

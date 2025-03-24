#include <cilk/cilk.h>
#include <cstdlib>
#include <iostream>

int constexpr size = 1 << 25;

void work() {
  for (int i = 0; i < 100000000; i++)
    i = i;
}

__attribute__((noinline))
void f(int* ptr) {
  free(ptr); // TS 0: Free pointer
}

__attribute__((noinline))
void write(int * ptr) {
  work();
  work();
  ptr[0] = 7; // TS 2: Write to pointer
  std::cout << "write" << std::endl;
}

__attribute__((noinline))
void verify_calloc(int* arr_goal) {
  work();
  
  int* arr = (int*)calloc(sizeof(int), size); // TS 1: Happen to calloc free'd memory
  while(arr != arr_goal) {
    arr = (int*)calloc(sizeof(int), size); // TS 1: Happen to calloc free'd memory (FORCE)
  }
  std::cout << "CALLOC: " << arr << std::endl;

  //int* arr = (int*) malloc(sizeof(int) * size);
  work();
  work();
  std::cout << "checking..." << std::endl; //TS 3: Read alloc'd array
  for (size_t i = 0; i < size; i++)
    if (arr[i] != 0)
      std::cout << "BAD!" << std::endl;
  std::cout << "done." << std::endl;
  free(arr);
}

int main() {

  int* arr = (int*)calloc(sizeof(int), size);
  std::cout << " ALLOC: " << arr << std::endl;
  cilk_spawn write(arr);
  cilk_spawn f(arr);
  cilk_spawn verify_calloc(arr);
  cilk_sync;
}

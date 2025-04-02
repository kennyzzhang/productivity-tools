#include <cilk/cilk.h>
#include <cstdlib>
#include <iostream>

int constexpr size = 1 << 25;
int* arr;

__attribute__((noinline))
void f() {
  free(arr); // TS 0: Free pointer
}

int main() {

  arr = (int*)calloc(sizeof(int), size);
  std::cout << " ALLOC: " << arr << std::endl;

  cilk_spawn f();
  
  int* local_ptr = (int*)calloc(sizeof(int), size);
  std::cout << "CALLOC: " << local_ptr << std::endl;
  local_ptr[0] = 50;
  arr[0] = 60;
  std::cout << "50 = " << local_ptr[0] << std::endl;

  cilk_sync;
}

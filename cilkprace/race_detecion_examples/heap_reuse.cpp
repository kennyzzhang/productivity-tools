#include <cilk/cilk.h>
#include <cstdlib>
#include <iostream>

int constexpr size = 1 << 10;

int main() {
  cilk_for(int i = 0; i < 100; i++)
  {
    int* arr = (int*)malloc(sizeof(int) * size);
    int* arr2 = new int[size];
//    std::cout << "Arr: " << arr << std::endl;
    arr[2] = 91;
    arr2[2] = 91;
    free(arr);
    delete[] arr2;
  }
}

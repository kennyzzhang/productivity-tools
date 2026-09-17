#include <stdio.h>
#include <cilk/cilk.h>

__attribute__((noinline))
void f(int* x, int* y){
  *x = *y;
}

int main() {
  int x = 1;
  int y = 2;
  int z = 3;
  cilk_spawn f(&x, &z);
  f(&y, &z);
  cilk_sync;
  return x + y;
}

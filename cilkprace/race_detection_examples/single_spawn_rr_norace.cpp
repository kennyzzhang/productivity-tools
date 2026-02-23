#include <stdio.h>
#include <cilk/cilk.h>

void f(int* x, int* y){
  *x = *y;
}

int main() {
  int x = 1;
  int y = 2;
  int z = 3;
  cilk_spawn f(&x, &z);
  f(&y, &z);
}

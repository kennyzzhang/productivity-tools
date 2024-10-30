#include <stdio.h>
#include <cilk/cilk.h>

void f(int* x, int v){
  *x = v;
}

int main() {
  int x = 0;
  cilk_spawn f(&x, 5);
  f(&x, 6);
}

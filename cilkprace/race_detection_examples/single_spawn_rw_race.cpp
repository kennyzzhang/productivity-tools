#include <stdio.h>
#include <cilk/cilk.h>
#include <unistd.h>

__attribute__((noinline)) void f(int* x, int v){
  *x = v;
  usleep(100);
}

int main() {
  int x = 0;
  int y = 0;
  cilk_spawn f(&x, 5);
  f(&y, x);
}

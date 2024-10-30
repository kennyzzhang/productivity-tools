#include <cilk/cilk.h>

void f(int* x, int v) {
  *x = v;
}

void g(int v) {
  int x;
  f(&x, v);
}

int main() {
  int x = 0;
  cilk_spawn g(5);
  cilk_spawn g(6);
}

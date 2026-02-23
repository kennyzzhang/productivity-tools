#include <iostream>
#include <cilk/cilk.h>

__attribute__((noinline))
void f(int* x, int v){
  *x = v; 
}

int main() {
  int x = 0;
  cilk_spawn f(&x, 5);
  f(&x, 6);
  std::cout << "X: " << x << std::endl;
}

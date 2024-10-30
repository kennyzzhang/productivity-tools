#include <iostream>
#include <cilk/cilk.h>

int p_fib(int n) {
  if (n < 2)
    return n;                   // base case
  int x, y;
  cilk_scope {                  // begin lexical scope of parallel region
    x = cilk_spawn p_fib(n-1);  // don't wait for function to return
    y = p_fib(n-2);             // may run in parallel with spawned function
  }                             // wait for spawned function if needed
  return x + y;
}

int main()
{
	std::cout << "fib: " << p_fib(6) << std::endl;
  return 0;
}

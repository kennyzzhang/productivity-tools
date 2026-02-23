#include <iostream>
#include <cilk/cilk.h>

void blah(int& n) {
  n = 2;
}

void bad_sync()
{
  cilk_sync;
}

int main()
{
  int x;
  for (int i = 0; i < 5; i++) {
    cilk_spawn blah(x);
    if (i == 2)
      bad_sync();
  }
  return 0;
}

//#include <iostream>
//#include <cilk/cilk.h>

void blah(int& n) {
  n = 2;
}

int main()
{
  int x;
  int y;
  cilk_scope {
    cilk_spawn blah(y);
    blah(x);
    cilk_spawn blah(x);
  }
  return 0;
}

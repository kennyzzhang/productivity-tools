#include <iostream>
//#include <cilk/cilk.h>


void blah(int& n, const char* msg) {
  std::cout << msg;
  //n = 2;
}

int main()
{
  int x;
  int y;
  blah(x, "base level ");
  cilk_spawn blah(y, "spawn0 ");
  cilk_scope {
    cilk_spawn blah(y, "spawn1 ");
    cilk_spawn blah(x, "spawn2 ");
    cilk_spawn blah(x, "spawn3 ");
    cilk_spawn blah(x, "spawn4 ");
    blah(x, "continue 1 ");
    cilk_spawn blah(x, "spawn 5 ");
    blah(x, "continue 2 ");
  }
  
  blah(y, "fully synced ");
  cilk_spawn blah(y, "newspawn1 ");
  cilk_spawn blah(y, "newspawn2 ");
  blah(y, "continue 3 ");
  cilk_sync;
  cilk_sync;
  cilk_sync;
  cilk_sync;
  cilk_sync;
  blah(x, "fully synced ");
  return 0;
}

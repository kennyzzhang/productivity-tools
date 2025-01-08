#include <cilk/cilk.h>
#include <stdlib.h>
#include <cstdlib>
#include <cstring>

void cpy(char* from, size_t len)
{
  memcpy(from, from, len);
}

int main()
{
  char arr[100];
  
  cilk_spawn cpy(arr, 20);
  cilk_spawn cpy(arr+10, 10);
}

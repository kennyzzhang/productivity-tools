#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "getoptions.h"
#include "timing.h"

#include <cilk/cilk.h>

#ifdef SERIAL
#include <cilk/cilk_stub.h>
#endif


/*
 * nqueen  4 = 2
 * nqueen  5 = 10
 * nqueen  6 = 4
 * nqueen  7 = 40
 * nqueen  8 = 92
 * nqueen  9 = 352
 * nqueen 10 = 724
 * nqueen 11 = 2680
 * nqueen 12 = 14200
 * nqueen 13 = 73712
 * nqueen 14 = 365596
 * nqueen 15 = 2279184
 */

/*
 * <a> contains array of <n> queen positions.  Returns 1
 * if none of the queens conflict, and returns 0 otherwise.
 */
int ok(int n, char *a) {

  for (int i = 0; i < n; i++) {
    char p = a[i];
    for (int j = i + 1; j < n; j++) {
      char q = a[j];
      if (q == p || q == p - (j - i) || q == p + (j - i))
        return 0;
    }
  }

  return 1;
}

int nqueens(int n, int j, char *a) {
  int solNum = 0;

  if (n == j) {
    return 1;
  }

  int *count = (int *)alloca(n * sizeof(int));
  (void)memset(count, 0, n * sizeof(int));

  cilk_scope {
    for (int i = 0; i < n; i++) {

      /***
       * ANGE: strictly speaking, this (alloca after spawn) is frowned
       * up on, but in this case, this is ok, because b returned by
       * alloca is only used in this iteration; later spawns don't
       * need to be able to access copies of b from previous iterations
       ***/
      /* b = (char *) alloca((j + 1) * sizeof (char)); */
      char *b_alloc = (char *)alloca((j + 1) * sizeof(char) + 31);
      char *b = (char *)(((uintptr_t)b_alloc + 31) & ~31);
      memcpy(b, a, j * sizeof(char));
      b[j] = i;

      if (ok(j + 1, b)) {
        count[i] = cilk_spawn nqueens(n, j + 1, b);
      }
    }
  }

  // #pragma clang loop vectorize(disable)
  for (int i = 0; i < n; i++) {
    solNum += count[i];
  }

  return solNum;
}

int main(int argc, char *argv[]) {

  int n = 12;

  const char *specifiers[] = {"-n", "-i", 0};
  int opt_types[] = {INTARG, INTARG, 0};
  int iterations = 1;

  get_options(argc, argv, specifiers, opt_types, &n, &iterations);

  fprintf(stderr, "Running %s with n = %d.\n", argv[0], n);

  char *a = (char *)alloca(n * sizeof(char));

  int res = 0;
  for (int iter = 0; iter < iterations; iter++) {
    timer_start();
    res = nqueens(n, 0, a);
    record_time(timer_stop_ms());
  }

  report_time();

  if (res == 0) {
    fprintf(stderr, "No solution found.\n");
  } else {
    fprintf(stderr, "Total number of solutions : %d\n", res);
  }

  return 0;
}

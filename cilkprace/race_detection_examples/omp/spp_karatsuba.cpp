// Karatsuba polynomial multiplication, ported to OpenCilk from the case study
// in "Structured Parallel Programming" (McCool, Robison, Reinders, 2012). This
// is one of the benchmarks PTRacer (Yoga et al., FSE 2016) used to compare
// against SPD3. It follows the book's TBB version, which PTRacer ran, with
// tbb::parallel_invoke written as cilk_spawn and the timing harness replaced
// by timing.h.
//
// Usage: spp_karatsuba [-i iterations] [-n degree] [-r multiplies per timing]
//
// Copyright (c) 2012 Michael McCool, Arch Robison, and James Reinders.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of the authors or Elsevier nor the names of its
//   contributors may be used to endorse or promote products derived from this
//   software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// OpenMP-task port of ../spp_karatsuba.cpp for detector comparison.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../timing.h"

// Assumed to hold 32 bits
typedef unsigned int T;

template <typename U> class temp_space {
  static const size_t n = 4096 / sizeof(U);
  U temp[n];
  U *base;

public:
  U *data() { return base; }
  U &operator[](size_t k) { return base[k]; }
  temp_space(size_t size) { base = size <= n ? temp : new U[size]; }
  ~temp_space() {
    if (base != temp)
      delete[] base;
  }
};

// Polynomial multiplication C=A*B that takes quadratic time.
// A = a[0:n], B = b[0:n], C = c[0:2*n-1]
// This routine is used for the "small n" base case in Karatsuba multiplication.
static void simple_mul(T c[], const T a[], const T b[], size_t n) {
  for (size_t j = 0; j < 2 * n - 1; ++j)
    c[j] = 0;
  for (size_t i = 0; i < n; ++i)
    for (size_t j = 0; j < n; ++j)
      c[i + j] += a[i] * b[j];
}

const size_t CutOff = 128;

// Polynomial multiplication C=A*B.
// A = a[0:n], B = b[0:n], C = c[0:2*n-1]
static void karatsuba(T c[], const T a[], const T b[], size_t n) {
  if (n <= CutOff) {
    simple_mul(c, a, b, n);
  } else {
    size_t m = n / 2;
    temp_space<T> s(4 * (n - m));
    T *t = s.data();
    // Set c[0:n-1] = t_0
#pragma omp task
    karatsuba(c, a, b, m);
    // Set c[2*m:n-1] = t_2
#pragma omp task
    karatsuba(c + 2 * m, a + m, b + m, n - m);
    {
      T *a_ = t + 2 * (n - m), *b_ = a_ + (n - m);
      for (size_t j = 0; j < m; ++j) {
        a_[j] = a[j] + a[m + j];
        b_[j] = b[j] + b[m + j];
      }
      if (n & 1) {
        a_[m] = a[2 * m];
        b_[m] = b[2 * m];
      }
      // Set t = t_1
      karatsuba(t, a_, b_, n - m);
    }
#pragma omp taskwait
    // Set t = t_1 - t_0 - t_2
    for (size_t j = 0; j < 2 * m - 1; ++j)
      t[j] -= c[j] + c[2 * m + j];
    // Add (t_1 - t_0 - t_2) K into final product.
    c[2 * m - 1] = 0;
    for (size_t j = 0; j < 2 * m - 1; ++j)
      c[m + j] += t[j];
    if (n & 1)
      for (size_t j = 0; j < 2; ++j)
        c[3 * m - 1 + j] += t[2 * m - 1 + j] - c[4 * m - 1 + j];
  }
}

static void fill_random(T x[], size_t n) {
  const unsigned Radix = 10;
  for (size_t k = 0; k < n; ++k)
    x[k] = std::rand() % Radix;
}

int main(int argc, char *argv[]) {
  int iters = 1;
  size_t n = 10000;  // the book's nMax
  int reps = 200;  // about 0.3 s uninstrumented on one worker
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "-i"))
      iters = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "-n"))
      n = strtoul(argv[i + 1], nullptr, 0);
    else if (!strcmp(argv[i], "-r"))
      reps = atoi(argv[i + 1]);
  }
  std::srand(2);
  T *x = new T[n], *y = new T[n], *z = new T[2 * n], *w = new T[2 * n];
  fill_random(x, n);
  fill_random(y, n);

#pragma omp parallel
#pragma omp single
  for (int it = 0; it < iters; ++it) {
    timer_start();
    for (int r = 0; r < reps; ++r)
      karatsuba(z, x, y, n);
    record_time(timer_stop_ms());
  }

  // Check against the quadratic algorithm.
  simple_mul(w, x, y, n);
  for (size_t i = 0; i < 2 * n - 1; ++i) {
    if (w[i] != z[i]) {
      fprintf(stderr, "karatsuba: wrong result at %zu\n", i);
      return 1;
    }
  }
  report_time();
  delete[] x;
  delete[] y;
  delete[] z;
  delete[] w;
  return 0;
}

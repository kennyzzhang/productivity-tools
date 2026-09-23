// Parallel sorts, ported to OpenCilk from the sorting case studies in
// "Structured Parallel Programming" (McCool, Robison, Reinders, 2012). This is
// the "sort" benchmark PTRacer (Yoga et al., FSE 2016) used to compare against
// SPD3. The book's driver times each of its parallel sorts; this does the same
// for its four Cilk versions (recursive quicksort, semi-recursive quicksort,
// merge sort, sample sort) and reports their total. Array notation from the
// Cilk Plus versions is written as the loops of the book's TBB versions, and
// the timing harness is replaced by timing.h.
//
// Usage: spp_sort [-i iterations] [-m sorts] [-n keys per sort]
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

#include <cilk/cilk.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "timing.h"

// The book's default key type (MODE==UNIFORM_DISTRIBUTION).
typedef int T;

static unsigned Random() { return rand() * (RAND_MAX + 1u) + rand(); }

// ---- common/quicksort_util.h ----

// Size of parallel base case.
ptrdiff_t QUICKSORT_CUTOFF = 500;

// Choose median of three keys.
static T *median_of_three(T *x, T *y, T *z) {
  return *x < *y ? *y < *z ? y : *x < *z ? z : x
                 : *z < *y ? y : *z < *x ? z : x;
}

// Choose a partition key as median of medians.
static T *choose_partition_key(T *first, T *last) {
  size_t offset = (last - first) / 8;
  return median_of_three(
      median_of_three(first, first + offset, first + offset * 2),
      median_of_three(first + offset * 3, first + offset * 4,
                      last - (3 * offset + 1)),
      median_of_three(last - (2 * offset + 1), last - (offset + 1), last - 1));
}

// Choose a partition key and partition [first...last) with it.
// Returns pointer to where the partition key is in partitioned sequence.
// Returns NULL if all keys in [first...last) are equal.
static T *divide(T *first, T *last) {
  // Move partition key to front.
  std::swap(*first, *choose_partition_key(first, last));
  // Partition
  T key = *first;
  T *middle =
      std::partition(first + 1, last, [=](const T &x) { return x < key; }) - 1;
  if (middle != first) {
    // Move partition key to between the partitions
    std::swap(*first, *middle);
  } else {
    // Check if all keys are equal
    if (last == std::find_if(first + 1, last, [=](const T &x) { return key < x; }))
      return NULL;
  }
  return middle;
}

// ---- cilkplus/quicksort_cilk_recursive.h ----
namespace Ex1 {
void parallel_quicksort(T *first, T *last) {
  if (last - first <= QUICKSORT_CUTOFF) {
    std::sort(first, last);
  } else {
    // Divide
    if (T *middle = divide(first, last)) {
      // Conquer subproblems in parallel
      cilk_spawn parallel_quicksort(first, middle);
      parallel_quicksort(middle + 1, last);
      // no cilk_sync needed here because of implicit one later
    }
  }
  // Implicit cilk_sync when function returns
}
} // namespace Ex1

// ---- cilkplus/quicksort_cilk_semirecursive.h ----
namespace Ex2 {
void parallel_quicksort(T *first, T *last) {
  while (last - first > QUICKSORT_CUTOFF) {
    // Divide
    T *middle = divide(first, last);
    if (!middle)
      return;

    // Now have two subproblems: [first..middle) and (middle..last)
    if (middle - first < last - (middle + 1)) {
      // Left problem [first..middle) is smaller, so spawn it.
      cilk_spawn parallel_quicksort(first, middle);
      // Solve right subproblem in next iteration.
      first = middle + 1;
    } else {
      // Right problem (middle..last) is smaller, so spawn it.
      cilk_spawn parallel_quicksort(middle + 1, last);
      // Solve left subproblem in next iteration.
      last = middle;
    }
  }
  // Base case
  std::sort(first, last);
}
} // namespace Ex2

// ---- serial/serial_merge.h, cilkplus/merge_cilk.h, merge_sort_cilk.h ----
namespace Ex3 {
void serial_merge(T *xs, T *xe, T *ys, T *ye, T *zs) {
  while (xs != xe && ys != ye) {
    bool which = *ys < *xs;
    *zs++ = std::move(which ? *ys++ : *xs++);
  }
  std::move(xs, xe, zs);
  std::move(ys, ye, zs);
}

// merge sequences [xs,xe) and [ys,ye) to output [zs,(xe-xs)+(ye-ys)
void parallel_merge(T *xs, T *xe, T *ys, T *ye, T *zs) {
  const size_t MERGE_CUT_OFF = 2000;
  if (size_t(xe - xs + ye - ys) <= MERGE_CUT_OFF) {
    serial_merge(xs, xe, ys, ye, zs);
  } else {
    T *xm, *ym;
    if (xe - xs < ye - ys) {
      ym = ys + (ye - ys) / 2;
      xm = std::upper_bound(xs, xe, *ym);
    } else {
      xm = xs + (xe - xs) / 2;
      ym = std::lower_bound(ys, ye, *xm);
    }
    T *zm = zs + (xm - xs) + (ym - ys);
    cilk_spawn parallel_merge(xs, xm, ys, ym, zs);
    /*nospawn*/ parallel_merge(xm, xe, ym, ye, zm);
    // implicit cilk_sync
  }
}

// sorts [xs,xe).  zs[0:xe-xs) is temporary buffer supplied by caller.
// result is in [xs,xe) if inplace==true, otherwise in zs[0:xe-xs)
void parallel_merge_sort(T *xs, T *xe, T *zs, bool inplace) {
  const size_t SORT_CUT_OFF = 500;
  if (size_t(xe - xs) <= SORT_CUT_OFF) {
    std::stable_sort(xs, xe);
    if (!inplace)
      std::move(xs, xe, zs);
  } else {
    T *xm = xs + (xe - xs) / 2;
    T *zm = zs + (xm - xs);
    T *ze = zs + (xe - xs);
    cilk_spawn parallel_merge_sort(xs, xm, zs, !inplace);
    /*nospawn*/ parallel_merge_sort(xm, xe, zm, !inplace);
    cilk_sync;
    if (inplace)
      parallel_merge(zs, zm, zm, ze, xs);
    else
      parallel_merge(xs, xm, xm, xe, zs);
  }
}

void sort(T *xs, T *xe) {
  T *zs = new T[xe - xs];
  parallel_merge_sort(xs, xe, zs, true);
  delete[] zs;
}
} // namespace Ex3

// ---- common/sample_sort_util.h and the cilkplus sample sort pieces ----
namespace Ex4 {
using Ex2::parallel_quicksort;

// Max number of bins.  Must not exceed 256.
const size_t M_MAX = 32;

const size_t SAMPLE_SORT_CUT_OFF = 2000;

size_t floor_lg2(size_t n) {
  size_t k = 0;
  for (; n > 1; n >>= 1)
    ++k;
  return k;
}

size_t choose_number_of_bins(size_t n) {
  const size_t BIN_CUTOFF = 1024;
  return std::min(M_MAX, size_t(1) << floor_lg2(n / BIN_CUTOFF));
}

typedef unsigned char bindex_type;

// Assumes that m is a power of 2
void build_sample_tree(const T *xs, const T *xe, T tree[], size_t m) {
  // Compute oversampling coefficient o as approximately log(xe-xs)
  assert(m <= M_MAX);
  size_t o = floor_lg2(xe - xs);
  const size_t O_MAX = 8 * (sizeof(size_t));
  size_t n_sample = o * m - 1;
  T tmp[O_MAX * M_MAX - 1];
  size_t r = (xe - xs - 1) / (n_sample - 1);
  // Generate oversampling
  for (size_t i = 0; i < n_sample; ++i)
    tmp[i] = xs[i * r];
  // Sort the samples
  std::sort(tmp, tmp + n_sample);
  // Select samples and put them into the tree
  size_t step = n_sample + 1;
  for (size_t level = 1; level < m; level *= 2) {
    for (size_t k = 0; k < level; ++k)
      tree[level - 1 + k] = tmp[step / 2 - 1 + k * step];
    step /= 2;
  }
}

// Set bindex[0..n) to the bin index of each key in x[0..n), using the given
// implicit binary tree with m-1 entries.
void map_keys_to_bins(const T x[], size_t n, const T tree[], size_t m,
                      bindex_type bindex[], size_t freq[]) {
  size_t d = floor_lg2(m);
  for (size_t j = 0; j < m; ++j)
    freq[j] = 0;
  for (size_t i = 0; i < n; ++i) {
    size_t k = 0;
    for (size_t j = 0; j < d; ++j)
      k = 2 * k + 2 - (x[i] < tree[k]);
    ++freq[bindex[i] = k - (m - 1)];
  }
}

void bin(T *xs, T *xe, size_t m, T *y, size_t tally[M_MAX][M_MAX]) {
  T tree[M_MAX - 1];
  build_sample_tree(xs, xe, tree, m);

  size_t block_size = ((xe - xs) + m - 1) / m;
  bindex_type *bindex = new bindex_type[xe - xs];
  cilk_for(size_t i = 0; i < m; ++i) {
    size_t js = i * block_size;
    size_t je = std::min(js + block_size, size_t(xe - xs));

    // Map keys to bins
    size_t freq[M_MAX];
    map_keys_to_bins(xs + js, je - js, tree, m, bindex + js, freq);

    // Compute where each bucket starts
    T *dst[M_MAX];
    size_t s = 0;
    for (size_t j = 0; j < m; ++j) {
      dst[j] = y + js + s;
      s += freq[j];
      tally[i][j] = s;
    }

    // Scatter keys into their respective buckets
    for (size_t j = js; j < je; ++j)
      *dst[bindex[j]]++ = std::move(xs[j]);
  }
  delete[] bindex;
}

void repack_and_subsort(T *xs, T *xe, size_t m, const T *y,
                        const size_t tally[M_MAX][M_MAX]) {
  // Compute column sums of tally, forming the running sum of bin sizes.
  size_t col_sum[M_MAX];
  for (size_t j = 0; j < m; ++j)
    col_sum[j] = 0;
  for (size_t i = 0; i < m; ++i)
    for (size_t j = 0; j < m; ++j)
      col_sum[j] += tally[i][j];
  assert(col_sum[m - 1] == size_t(xe - xs));

  // Copy buckets into their bins and do the subsorts
  size_t block_size = ((xe - xs) + m - 1) / m;
  cilk_for(size_t j = 0; j < m; ++j) {
    T *x_bin = xs + (j == 0 ? 0 : col_sum[j - 1]);
    T *x = x_bin;
    for (size_t i = 0; i < m; ++i) {
      const T *src_row = y + i * block_size;
      x = std::move(src_row + (j == 0 ? 0 : tally[i][j - 1]),
                    src_row + tally[i][j], x);
    }
    parallel_quicksort(x_bin, x);
  }
}

void parallel_sample_sort(T *xs, T *xe) {
  if (size_t(xe - xs) <= SAMPLE_SORT_CUT_OFF) {
    parallel_quicksort(xs, xe);
  } else {
    size_t m = choose_number_of_bins(xe - xs);
    size_t tally[M_MAX][M_MAX];
    T *y = new T[xe - xs];
    bin(xs, xe, m, y, tally);
    repack_and_subsort(xs, xe, m, y, tally);
    delete[] y;
  }
}
} // namespace Ex4

// ---- driver ----

static size_t M = 10;       // sorts per timing, as in the book
static size_t N = 1000000;  // keys per sort, as in the book
static T *Unsorted, *Expected, *Actual;

typedef void (*Sort)(T *, T *);

// Times M sorts of fresh copies of the input; returns milliseconds.
static unsigned long long time_sort(Sort sort, const char *what) {
  std::copy(Unsorted, Unsorted + M * N, Actual);
  timer_start();
  for (size_t i = 0; i < M; ++i)
    sort(Actual + i * N, Actual + (i + 1) * N);
  unsigned long long ms = timer_stop_ms();
  for (size_t k = 0; k < M * N; ++k) {
    if (Actual[k] != Expected[k]) {
      fprintf(stderr, "spp_sort: wrong result for %s\n", what);
      exit(1);
    }
  }
  return ms;
}

int main(int argc, char *argv[]) {
  int iters = 1;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "-i"))
      iters = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "-m"))
      M = strtoul(argv[i + 1], nullptr, 0);
    else if (!strcmp(argv[i], "-n"))
      N = strtoul(argv[i + 1], nullptr, 0);
  }
  srand(2);
  Unsorted = new T[M * N];
  Expected = new T[M * N];
  Actual = new T[M * N];
  for (size_t i = 0; i < M; ++i) {
    for (size_t j = 0; j < N; ++j)
      Unsorted[i * N + j] = Random();
    std::copy(Unsorted + i * N, Unsorted + (i + 1) * N, Expected + i * N);
    std::sort(Expected + i * N, Expected + (i + 1) * N);
  }

  for (int it = 0; it < iters; ++it) {
    unsigned long long ms = 0;
    ms += time_sort(Ex1::parallel_quicksort, "quicksort recursive");
    ms += time_sort(Ex2::parallel_quicksort, "quicksort semi-recursive");
    ms += time_sort(Ex3::sort, "mergesort");
    ms += time_sort(Ex4::parallel_sample_sort, "samplesort");
    record_time(ms);
  }
  report_time();
  delete[] Unsorted;
  delete[] Expected;
  delete[] Actual;
  return 0;
}

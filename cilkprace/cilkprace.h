#pragma once

#pragma GCC visibility push(default)

#include "csan.h"
#include <cassert>
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <cilk/cilkprace_ablation.h>
#include <cilk/os_label.h>
#include "shadow_label.h"
#include <cmath>
#include <csi/csi.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <dlfcn.h>
#include <mutex>

#include <shadowmem_reservevm.h>
#include <shadowmem_pagetable.h>

#define TRACE_CALLS 1
#undef TRACE_CALLS

#ifndef CILKPRACE_VIS
#define CILKPRACE_VIS
#endif
#define CILKTOOL_API extern "C" __attribute__((visibility("default")))
#define CILKSAN_API extern "C" CILKPRACE_VIS __attribute__((visibility("default")))

extern __attribute__((visibility("default"))) bool HAS_INIT;
extern __attribute__((visibility("default"))) bool CHECKING;
extern __attribute__((visibility("default"))) int CHECKING_DISABLED;

#ifndef CILKPRACE_GRANULARITY
#define CILKPRACE_GRANULARITY 4
#endif

// Sizes of live heap blocks, for frees and reallocs. Cilksan keeps the same
// map; this one is sharded under locks because hooks run in parallel. Each
// shard is an open-addressing table (no STL: the hooks are inlined into the
// program as bitcode). See cilkprace.cpp.
class heap_sizes_t {
  static constexpr unsigned NSHARDS = 64;
  struct alignas(64) shard_t {
    std::mutex lock;
    uintptr_t *keys = nullptr;  // 0: empty, 1: removed
    size_t *sizes = nullptr;
    size_t cap = 0, used = 0;   // used counts removed slots too
  } shards[NSHARDS];
  static shard_t &grow(shard_t &s);

public:
  void insert(uintptr_t addr, size_t size);
  // Removes addr's entry; returns false if there is none.
  bool remove(uintptr_t addr, size_t &size);
};

// The hook that freed a block, for the race report: its label, and which
// source-location table its id indexes. The *_call sites are library-call
// hooks, which the compiler uses when it doesn't recognize malloc and free as
// allocation functions (at -O0).
enum class free_site_t { free, realloc, free_call, realloc_call };

class CilkpraceImpl_t {
  shadowmem_reservevm<shadow_label, CILKPRACE_GRANULARITY> shadow_mem;
  // Never destroyed: frees keep coming during exit.
  heap_sizes_t &heap_sizes = *new heap_sizes_t;

// Assuming shadow_label is 2^10 bytes, pointers are 2^3 bytes,
// and virtual addresses are 48 bits.
// Granularity 4 means 46 bits in page table.
//  shadowmem_pagetable<shadow_label, 4, 27, 19> shadow_mem;
//  shadowmem_pagetable<shadow_label, 4, 18, 18, 10> shadow_mem;
  bool ignore_stdlib_races;

public:
  CilkpraceImpl_t();
  ~CilkpraceImpl_t();

  bool is_benign_stdlib_race(uintptr_t race_addr);

  void report_write_race(uintptr_t addr, csi_id_t store_id,
                         const os_label& cur_lab, const shadow_label& lab);

  void report_read_race(uintptr_t addr, csi_id_t load_id,
                        const os_label& cur_lab, const shadow_label& lab);


  // always_inline for the same reason as register_read.
  __attribute__((always_inline))
  void register_write(uintptr_t beg, size_t num_bytes,
                      csi_id_t store_id,
                      const os_label& cur_lab);

  void register_write(uintptr_t beg, size_t num_bytes,
                      csi_id_t store_id);

  // always_inline for the same reason as shadow_label::does_read_race: forcing
  // that one inline made this function the one that crossed the inliner's
  // threshold, and every read then called it out of line.
  __attribute__((always_inline))
  void register_read(uintptr_t beg, size_t num_bytes,
                     csi_id_t load_id,
                     const os_label& cur_lab);

  void register_read(uintptr_t beg, size_t num_bytes,
                     csi_id_t load_id);

  void register_alloca(uintptr_t beg, size_t num_bytes);

  void register_allocfn(uintptr_t addr, size_t nb);

  void register_alloc_strdup(uintptr_t addr, const char *str);

  // Heap and stack semantics follow Cilksan's (see cilkprace.cpp).
  void register_frame(uintptr_t bp, uintptr_t sp);

  void register_free(uintptr_t beg, size_t num_bytes, csi_id_t free_id,
                     free_site_t site);

  void register_heap_free(uintptr_t addr, csi_id_t free_id, free_site_t site);

  void register_realloc_begin(uintptr_t oldaddr, csi_id_t allocfn_id);

  void register_realloc_end(uintptr_t addr, size_t new_size,
                            uintptr_t oldaddr);

  void report_free_race(uintptr_t addr, csi_id_t free_id, free_site_t site);

  void advance_stack_frame(uintptr_t addr);
  void restore_stack(const csi_id_t call_id, uintptr_t addr);
};

extern __attribute__((visibility("default"))) CilkpraceImpl_t tool_instance;

// FIXME: Hardcoded for now
/*static*/ inline bool should_check() {
  return HAS_INIT;
}

void check_read_bytes(csi_id_t call_id, uintptr_t ptr, size_t len);
void check_read_bytes(csi_id_t call_id, const void *ptr, size_t len);
void check_write_bytes(csi_id_t call_id, uintptr_t ptr, size_t len);
void check_write_bytes(csi_id_t call_id, const void *ptr, size_t len);

#pragma GCC visibility pop

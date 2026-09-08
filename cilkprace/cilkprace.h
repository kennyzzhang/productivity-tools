#pragma once

#pragma GCC visibility push(default)

#include "csan.h"
#include <cassert>
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <cilk/os_label.h>
#include <cmath>
#include <csi/csi.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <dlfcn.h>

#include <shadowmem_reservevm.h>
#include <shadowmem_pagetable.h>

#include "stack.h"

#define TRACE_CALLS 1
#undef TRACE_CALLS

#ifndef CILKPRACE_VIS
#define CILKPRACE_VIS
#endif
#define CILKTOOL_API extern "C" __attribute__((visibility("default")))
#define CILKSAN_API extern "C" CILKPRACE_VIS __attribute__((visibility("default")))

extern __attribute__((visibility("default"))) bool HAS_INIT;

// Stack structures for keeping track of MAAP (May Access Alias in Parallel)
// information inserted by the compiler before a call.
enum class MAAP_t : uint8_t {
  NoAccess = 0,
  Mod = 1,
  Ref = 2,
  ModRef = Mod | Ref,
  NoAlias = 4,
};

using MAAPstack = Stack_t<std::pair<csi_id_t, MAAP_t>>;
using ustack = Stack_t<unsigned>;
using pstack = Stack_t<uint8_t>;

// Reducer functions for keeping track of MAAPs
void init_MAAPstack(void *view);
void reduce_MAAPstack(void *left_view, void *right_view);
typedef MAAPstack cilk_reducer(init_MAAPstack,
                               reduce_MAAPstack) MAAPstack_reducer;

void init_ustack(void *view);
void reduce_ustack(void *left_view, void *right_view);
typedef ustack cilk_reducer(init_ustack, reduce_ustack) ustack_reducer;

extern __attribute__((visibility("default"))) MAAPstack_reducer MAAPs;
extern __attribute__((visibility("default"))) ustack_reducer MAAP_counts;

class CilkpraceImpl_t {
  shadowmem_reservevm<shadow_label, 4> shadow_mem;

// Assuming shadow_label is 2^10 bytes, pointers are 2^3 bytes,
// and virtual addresses are 48 bits.
// Granularity 4 means 46 bits in page table.
//  shadowmem_pagetable<shadow_label, 4, 27, 19> shadow_mem;
//  shadowmem_pagetable<shadow_label, 4, 18, 18, 10> shadow_mem;
  bool ignore_stdlib_races;

public:
  CilkpraceImpl_t();
  ~CilkpraceImpl_t();

  __attribute__((noinline, cold, preserve_most))
  bool is_benign_stdlib_race(uintptr_t race_addr);

  __attribute__((noinline, cold, preserve_most, visibility("default")))
  void report_write_race(uintptr_t addr, csi_id_t store_id,
                         const os_label& cur_lab, const shadow_label& lab);

  __attribute__((noinline, cold, preserve_most, visibility("default")))
  void report_read_race(uintptr_t addr, csi_id_t load_id,
                        const os_label& cur_lab, const shadow_label& lab);

  void register_write(uintptr_t beg, size_t num_bytes,
                      csi_id_t store_id,
                      const os_label& cur_lab);

  void register_write(uintptr_t beg, size_t num_bytes,
                      csi_id_t store_id);

  void register_read(uintptr_t beg, size_t num_bytes,
                     csi_id_t load_id,
                     const os_label& cur_lab);

  void register_read(uintptr_t beg, size_t num_bytes,
                     csi_id_t load_id);

  void register_alloca(uintptr_t beg, size_t num_bytes);

  void register_allocfn(uintptr_t addr, size_t nb);

  void register_alloc_strdup(uintptr_t addr, const char *str);

  void register_free(uintptr_t addr);

  void advance_stack_frame(uintptr_t addr);
  void restore_stack(const csi_id_t call_id, uintptr_t addr);
};

extern __attribute__((visibility("default"))) CilkpraceImpl_t tool_instance;

// FIXME: Hardcoded for now
/*static*/ inline bool should_check() {
  return HAS_INIT;
}

void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal, uintptr_t ptr,
                      size_t len);
void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal, const void *ptr,
                      size_t len);
void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal, uintptr_t ptr,
                       size_t len);
void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal, const void *ptr,
                       size_t len);

#pragma GCC visibility pop

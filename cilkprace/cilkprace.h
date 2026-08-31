#pragma once
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
#include <ostream>
#include <unistd.h>
#include <dlfcn.h>

#include <shadowmem_reservevm.h>
#include <shadowmem_pagetable.h>

#include "outs_red.h"
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

// Stack structures for keeping track of MAAPs for pointer arguments to function
// calls
static void init_MAAPstack(void *view) {
#if TRACE_CALLS
  std::cerr << "init MAAPSTACK" << std::endl;
#endif
  new (view) MAAPstack();
}

static void reduce_MAAPstack(void *left_view, void *right_view) {
#if TRACE_CALLS
  std::cerr << "reduce MAAPSTACK" << std::endl;
#endif
  MAAPstack *left = static_cast<MAAPstack *>(left_view);
  MAAPstack *right = static_cast<MAAPstack *>(right_view);
  
  int32_t net_change = static_cast<int32_t>(right->size()) - 1;
  if (net_change < 0) {
    for (int32_t i = 0; i < -net_change; ++i) left->pop();
  } else if (net_change > 0) {
    for (int32_t i = 0; i < net_change; ++i) {
      left->push_back(right->from_back(net_change - 1 - i));
    }
  }
  
  right->~MAAPstack();
}

typedef MAAPstack cilk_reducer(init_MAAPstack,
                               reduce_MAAPstack) MAAPstack_reducer;

static void init_ustack(void *view) {
#if TRACE_CALLS
  std::cerr << "init ustack" << std::endl;
#endif
  new (view) ustack();
}

static void reduce_ustack(void *left_view, void *right_view) {
#if TRACE_CALLS
  std::cerr << "reduce ustack" << std::endl;
#endif
  ustack *left = static_cast<ustack *>(left_view);
  ustack *right = static_cast<ustack *>(right_view);
  
  int32_t net_change = static_cast<int32_t>(right->size()) - 1;
  if (net_change < 0) {
    for (int32_t i = 0; i < -net_change; ++i) left->pop();
  } else if (net_change > 0) {
    for (int32_t i = 0; i < net_change; ++i) {
      left->push_back(right->from_back(net_change - 1 - i));
    }
  }
  
  right->~ustack();
}

typedef ustack cilk_reducer(init_ustack, reduce_ustack) ustack_reducer;

extern MAAPstack_reducer MAAPs;
extern ustack_reducer MAAP_counts;

// FIXME
__attribute__((always_inline)) /*static*/ inline bool is_execution_parallel() {
  return !__cilkrts_get_current_os_label()->is_serial();
}

class CilkpraceImpl_t {
  shadowmem_reservevm<shadow_label, 4> shadow_mem;

// Assuming shadow_label is 2^10 bytes, pointers are 2^3 bytes,
// and virtual addresses are 48 bits.
// Granularity 4 means 46 bits in page table.
//  shadowmem_pagetable<shadow_label, 4, 27, 19> shadow_mem;
//  shadowmem_pagetable<shadow_label, 4, 18, 18, 10> shadow_mem;
  bool ignore_stdlib_races;

public:
  CilkpraceImpl_t() {
#ifdef TRACE_CALLS
    outs_red << "HAS INIT" << std::endl;
#endif
    const char *env_val = getenv("CILKPRACE_IGNORE_STDLIB_RACES");
    if (env_val && strcmp(env_val, "0") == 0) {
      ignore_stdlib_races = false;
    } else {
      ignore_stdlib_races = true;
    }

    // Note that we start executing the program in series.
    // parallel_execution.push_back(0);
    // Push a default value of 0 onto the MAAP_counts stack, in case this
    // function contains get_MAAP calls.
    // MAAP_counts.push_back(0);
    HAS_INIT = true;
  }

  ~CilkpraceImpl_t() {}

  __attribute__((noinline, cold, preserve_most))
  bool is_benign_stdlib_race(uintptr_t race_addr) {
    if (!ignore_stdlib_races) return false;
    Dl_info info;
    if (dladdr((void*)race_addr, &info) && info.dli_sname) {
      if (strstr(info.dli_sname, "cout") != nullptr ||
          strstr(info.dli_sname, "cerr") != nullptr) {
        return true;
      }
    }
    return false;
  }

  __attribute__((noinline, cold, preserve_most, visibility("default")))
  void report_write_race(uintptr_t addr, csi_id_t store_id,
                         const os_label& cur_lab, const shadow_label& lab);

  __attribute__((noinline, cold, preserve_most, visibility("default")))
  void report_read_race(uintptr_t addr, csi_id_t load_id,
                        const os_label& cur_lab, const shadow_label& lab);

  __attribute__((always_inline))
  inline void register_write(uintptr_t beg, size_t num_bytes,
                             csi_id_t store_id,
                             const os_label& cur_lab) {
    if (__builtin_expect(num_bytes == 0, 0)) return;
    shadow_mem.for_each(beg, beg + num_bytes,
      [&](uintptr_t addr, shadow_label& lab) __attribute__((always_inline)) {
        if (__builtin_expect(lab.does_write_race(cur_lab), 0)) {
          report_write_race(addr, store_id, cur_lab, lab);
        }
      });
  }

  __attribute__((always_inline))
  inline void register_write(uintptr_t beg, size_t num_bytes,
                             csi_id_t store_id) {
    register_write(beg, num_bytes, store_id, *__cilkrts_get_current_os_label());
  }

  __attribute__((always_inline))
  inline void register_read(uintptr_t beg, size_t num_bytes,
                            csi_id_t load_id,
                            const os_label& cur_lab) {
    if (__builtin_expect(num_bytes == 0, 0)) return;
    shadow_mem.for_each(beg, beg + num_bytes,
      [&](uintptr_t addr, shadow_label& lab) __attribute__((always_inline)) {
        if (__builtin_expect(lab.does_read_race(cur_lab), 0)) {
          report_read_race(addr, load_id, cur_lab, lab);
        }
      });
  }

  __attribute__((always_inline))
  inline void register_read(uintptr_t beg, size_t num_bytes,
                            csi_id_t load_id) {
    register_read(beg, num_bytes, load_id, *__cilkrts_get_current_os_label());
  }

  void register_alloca(uintptr_t beg, size_t num_bytes) {
    if (__builtin_expect(num_bytes == 0, 0)) return;
    shadow_mem.for_each(beg, beg + num_bytes,
      [&](uintptr_t addr, shadow_label& lab) {
        memset(&lab, 0, sizeof(shadow_label));
      });
  }

  void register_allocfn(uintptr_t addr, size_t nb) {
    register_alloca(addr, nb);
  }

  void register_alloc_strdup(uintptr_t addr, const char *str) {
    if (addr && str)
      register_alloca(addr, strlen(str) + 1);
  }

  void register_free(uintptr_t addr) {
    outs_red << "UNHANDLED FREE" << std::endl;
  }

  void advance_stack_frame(uintptr_t addr) {
    outs_red << "UNHANDLED STACK ADVANCE" << std::endl;
  }
  void restore_stack(const csi_id_t call_id, uintptr_t addr) {
    outs_red << "UNHANDLED STACK RESTORE" << std::endl;
  }
};

extern __attribute__((visibility("default"))) CilkpraceImpl_t tool_instance;

// FIXME: Hardcoded for now
__attribute__((always_inline)) /*static*/ inline bool should_check() {
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

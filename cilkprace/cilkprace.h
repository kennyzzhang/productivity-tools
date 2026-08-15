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

#define CILKTOOL_API extern "C" __attribute__((visibility("default")))
#define CILKSAN_API extern "C" __attribute__((visibility("default")))

extern bool HAS_INIT;

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
  assert(right->size() == 0 && "Expected empty MAAPstack!");
  right->~MAAPstack();
}

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
  assert(right->size() == 0 && "Expected empty ustack!");
  right->~ustack();
}
static void init_pstack(void *view) {
#if TRACE_CALLS
  std::cerr << "init pstack" << std::endl;
#endif
  new (view) pstack();
}
static void reduce_pstack(void *left_view, void *right_view) {
#if TRACE_CALLS
  std::cerr << "reduce pstack" << std::endl;
#endif
  pstack *left = static_cast<pstack *>(left_view);
  pstack *right = static_cast<pstack *>(right_view);
  assert(right->size() == 0 && "Expected empty pstack!");
  right->~pstack();
}

typedef MAAPstack cilk_reducer(init_MAAPstack,
                               reduce_MAAPstack) MAAPstack_reducer;
typedef ustack cilk_reducer(init_ustack, reduce_ustack) ustack_reducer;
typedef pstack cilk_reducer(init_pstack, reduce_pstack) pstack_reducer;

extern pstack_reducer parallel_execution;
extern MAAPstack_reducer MAAPs;
extern ustack_reducer MAAP_counts;

// FIXME
__attribute__((always_inline)) /*static*/ inline bool is_execution_parallel() {
  return true; // parallel_execution.back();
}

class CilkpraceImpl_t {
  shadowmem_reservevm<shadow_label, 4> shadow_mem;
//  shadowmem_pagetable<shadow_label, 12, 12, 12, 12> shadow_mem;
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

  void register_write(uintptr_t beg, size_t num_bytes,
                      const source_loc_t *store) {
    const auto cur_lab = __cilkrts_get_os_label().label;
    shadow_mem.for_each(beg, beg + num_bytes, [&](uintptr_t addr, shadow_label& lab) {
      if (lab.does_write_race(cur_lab) && !is_benign_stdlib_race(addr)) {
        outs_red
            << "WRITE RACE ON BYTE " << std::hex << addr
            << std::dec << "(+" << shadow_mem.vmem_shadow_granularity << ") BY "
            << std::dec << cur_lab << " WITH "
            << lab
            << std::endl;
        outs_red << "BY " << std::dec << cur_lab
             << std::endl;
        if (store)
          outs_red << "@ " << store->filename << " Ln " << std::dec
                   << store->line_number << " Col " << std::dec
                   << store->column_number << std::endl;
        outs_red << "======================" << std::endl;
        _exit(EXIT_FAILURE);
      }
    });
  }

  void register_read(uintptr_t beg, size_t num_bytes,
                     const source_loc_t *store) {
    const auto cur_lab = __cilkrts_get_os_label().label;
    shadow_mem.for_each(beg, beg + num_bytes, [&](uintptr_t addr, shadow_label& lab) {
      if (lab.does_read_race(cur_lab) && !is_benign_stdlib_race(addr)) {
        outs_red
            << "READ RACE ON BYTE " << std::hex << addr
            << std::dec << "(+" << shadow_mem.vmem_shadow_granularity << ") BY "
            << std::dec << cur_lab << " WITH "
            << lab
            << std::endl;
        outs_red << "BY " << std::dec << cur_lab
                 << std::endl;
        if (store)
          outs_red << "@ " << store->filename << " Ln " << std::dec
                   << store->line_number << " Col " << std::dec
                   << store->column_number << std::endl;
        outs_red << "======================" << std::endl;
        _exit(EXIT_FAILURE);
      }
    });
  }

  void register_alloca(uintptr_t beg, size_t num_bytes) {
    shadow_mem.for_each(beg, beg + num_bytes, [&](uintptr_t addr, shadow_label& lab) {
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

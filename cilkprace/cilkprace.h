#pragma once
#include <cassert>
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <cilk/os_label.h>
#include <cmath>
#include <cstdlib>
#include <csi/csi.h>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sys/mman.h>
#include <unistd.h>
#include "csan.h"

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

  // Stack structures for keeping track of MAAPs for pointer arguments to function calls
  static void init_MAAPstack (void* view) {
#if TRACE_CALLS
    std::cerr << "init MAAPSTACK" << std::endl;
#endif
    new (view) MAAPstack();
  }

  static void reduce_MAAPstack (void* left_view, void* right_view) {
#if TRACE_CALLS
    std::cerr << "reduce MAAPSTACK" << std::endl;
#endif
    MAAPstack *left = static_cast<MAAPstack*>(left_view);
    MAAPstack *right = static_cast<MAAPstack*>(right_view);
    assert(right->size() == 0 && "Expected empty MAAPstack!");
    right->~MAAPstack();
  }

  static void init_ustack(void* view) {
#if TRACE_CALLS
    std::cerr << "init ustack" << std::endl;
#endif
    new (view) ustack();
  }
  static void reduce_ustack (void* left_view, void* right_view) {
#if TRACE_CALLS
    std::cerr << "reduce ustack" << std::endl;
#endif
    ustack *left = static_cast<ustack*>(left_view);
    ustack *right = static_cast<ustack*>(right_view);
    assert(right->size() == 0 && "Expected empty ustack!");
    right->~ustack();
  }
  static void init_pstack(void* view) {
#if TRACE_CALLS
    std::cerr << "init pstack" << std::endl;
#endif
    new (view) pstack();
  }
  static void reduce_pstack (void* left_view, void* right_view) {
#if TRACE_CALLS
    std::cerr << "reduce pstack" << std::endl;
#endif
    pstack *left = static_cast<pstack*>(left_view);
    pstack *right = static_cast<pstack*>(right_view);
    assert(right->size() == 0 && "Expected empty pstack!");
    right->~pstack();
  }

typedef MAAPstack cilk_reducer(init_MAAPstack, reduce_MAAPstack) MAAPstack_reducer;
typedef ustack cilk_reducer(init_ustack, reduce_ustack) ustack_reducer;
typedef pstack cilk_reducer(init_pstack, reduce_pstack) pstack_reducer;

extern pstack_reducer parallel_execution;

//FIXME
__attribute__((always_inline)) /*static*/ inline bool is_execution_parallel() {
  return true; //parallel_execution.back();
}

class CilkpraceImpl_t {
  static constexpr size_t vmem_bytes = 0x00007fffffffffff; 
  static constexpr size_t overhead_per_byte = sizeof(shadow_label);
  static constexpr size_t vmem_shadow_granularity = 4;
  static constexpr size_t bytes_per_entry = overhead_per_byte + vmem_shadow_granularity;
  static constexpr size_t vmem_user_addressable_bytes = vmem_bytes / bytes_per_entry * vmem_shadow_granularity;
  static constexpr size_t vmem_shadow_size = vmem_bytes - vmem_user_addressable_bytes;

  void* shadow_mem;

  shadow_label* addr_to_shadow(void* addr)
  {
    #ifdef TRACE_CALLS
    outs_red << "addr_to_shadow(" << std::hex << addr << ")" << std::endl;
    #endif
    assert((size_t)addr < vmem_bytes && "VMEM BYTES ASSUMPTION VIOLATION");

    //TODO: Bounds chekcing?
    // We have to be piecewise. But, we can imagine memory above us as glued on where we are.
    // That is, project the addresses above our shadow memory as if they were just above the lower addresses
    addr = (addr < shadow_mem) ? addr : (void*)((uint8_t*)addr - (uint8_t*)shadow_mem);

    #ifdef TRACE_CALLS
    outs_red << "addr = " << std::hex << addr << std::endl;
    #endif

    // Now we have to re-scale our address onto our shadow mapping. 
    // Fortunately, since each byte indexes into the array, we can just... index.
    shadow_label* shadow_addr = &((shadow_label*)shadow_mem)[(size_t)addr/vmem_shadow_granularity];
    
    if ((uint8_t*)shadow_addr < (uint8_t*)shadow_mem || (uint8_t*)shadow_addr >= (uint8_t*)shadow_mem + vmem_shadow_size) {
      return nullptr;
    }

    return shadow_addr;
  }


public:
  CilkpraceImpl_t() 
  {
#ifdef TRACE_CALLS
    outs_red << "HAS INIT" << std::endl;
#endif
    shadow_mem = mmap(nullptr, vmem_shadow_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (shadow_mem == (void*)-1) {
      perror("SHADOW MEM");
      exit(1);
    }
    //outs_red << "SHADOW MEM: " << shadow_mem << std::endl;
    outs_red << "Want mem size: " << vmem_shadow_size << " = 2^" << log2(vmem_shadow_size) << std::endl;
    //outs_red << "Class size (bytes): " << sizeof(os_label) << std::endl;
    
    // Note that we start executing the program in series.
    //parallel_execution.push_back(0);
    // Push a default value of 0 onto the MAAP_counts stack, in case this
    // function contains get_MAAP calls.
    //MAAP_counts.push_back(0);
    HAS_INIT = true;
  }

  ~CilkpraceImpl_t() {}


  void register_write(uint64_t addr, size_t num_bytes, const source_loc_t* store) {
    bool has_race = false;
    shadow_label* labels = addr_to_shadow((void*)addr);
    if (!labels) return; //TODO: Complain louder
    size_t num_granules = (num_bytes + vmem_shadow_granularity - 1) / vmem_shadow_granularity;
    // Clamp to not walk past end of shadow memory
    for (size_t i = 0; i < num_granules; i++)
    {
      bool race = labels[i].does_write_race(__cilkrts_get_os_label().label);
      if (race) outs_red << "WRITE RACE ON BYTE " << (void*)((uint8_t*) addr + i * vmem_shadow_granularity) << "(+" << vmem_shadow_granularity << ")" << std::endl;
      has_race |= race;
    }
    if (has_race) {
      outs_red << "BY " <<  __cilkrts_get_os_label().label << std::endl;
      if (store)
        outs_red << "@ " << store->filename << " Ln " << store->line_number << " Col " << store->column_number << std::endl;
      outs_red << "======================" << std::endl;
    }
  }

  void register_read(uint64_t addr, size_t num_bytes, const source_loc_t* store) {
    bool has_race = false;
    shadow_label* labels = addr_to_shadow((void*)addr);
    if (!labels) return; //TODO: Complain louder
    
    size_t num_granules = (num_bytes + vmem_shadow_granularity - 1) / vmem_shadow_granularity;
    for (size_t i = 0; i < num_granules; i++)
    {
      bool race = labels[i].does_read_race(__cilkrts_get_os_label().label);
      if (race) outs_red << "READ RACE ON BYTE " << (void*)((uint8_t*) addr + i * vmem_shadow_granularity) << "(+" << vmem_shadow_granularity << ")" << std::endl;
      has_race |= race;
    }
    if (has_race) {
      outs_red << "BY " <<  __cilkrts_get_os_label().label << std::endl;
      if (store)
        outs_red << "@ " << store->filename << " Ln " << store->line_number << " Col " << store->column_number << std::endl;
      outs_red << "======================" << std::endl;
    }
  }

  void register_alloca(const void* addr, size_t nb) {
    shadow_label* labels = addr_to_shadow((void*)addr);
    if (!labels) return;
    size_t num_granules = (nb + vmem_shadow_granularity - 1) / vmem_shadow_granularity;
    size_t shadow_bytes = num_granules * sizeof(shadow_label);
    uint8_t* start = (uint8_t*)labels;
    uint8_t* end = start + shadow_bytes;

    // Page-align inward for madvise
    static const size_t page_size = sysconf(_SC_PAGESIZE);
    uint8_t* page_start = (uint8_t*)(((uintptr_t)start + page_size - 1) & ~(page_size - 1));
    uint8_t* page_end   = (uint8_t*)((uintptr_t)end & ~(page_size - 1));

    if (page_start < page_end) {
      // memset sub-page head
      if (start < page_start)
        memset(start, 0, page_start - start);
      // Lazy zero the page-aligned bulk — kernel zeros on next access
      madvise(page_start, page_end - page_start, MADV_FREE);
      // memset sub-page tail
      if (page_end < end)
        memset(page_end, 0, end - page_end);
    } else {
      // Too small for madvise, just memset the whole thing
      memset(start, 0, shadow_bytes);
    }
  }

  void register_allocfn(const void* addr, size_t nb) {
   outs_red << "UNHANDLED ALLOCFN" << std::endl;
  }

  void register_alloc_strdup(const void* addr, const char* str) {
   outs_red << "UNHANDLED ALLOC_STRDUP" << std::endl;
  }

  void register_free(const void* addr) {
   outs_red << "UNHANDLED FREE" << std::endl;
  }

  void advance_stack_frame(uint64_t addr) { 
    outs_red << "UNHANDLED STACK ADVANCE" << std::endl;
  }
  void restore_stack(const csi_id_t call_id, uint64_t addr) { 
    outs_red << "UNHANDLED STACK RESTORE" << std::endl;
  }
};
//FIXME: Hardcoded for now
__attribute__((always_inline)) /*static*/ inline bool should_check() {
  return HAS_INIT;
}

void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal, uintptr_t ptr, size_t len);
void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal, const void*  ptr, size_t len);
void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal, uintptr_t ptr, size_t len);
void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal, const void*  ptr, size_t len);


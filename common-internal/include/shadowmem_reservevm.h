#pragma once

#include <cassert>
#include <cstdint>
#include <sys/mman.h>

#include "shadowmem.h"
#include "outs_red.h"

template<typename Value, size_t Granularity>
class shadowmem_reservevm : public shadowmem<shadowmem_reservevm<Value, Granularity>> {
  uintptr_t shadowmem;
  uintptr_t shadowmem_end;
public:
  static constexpr size_t vmem_bytes = 0x00007fffffffffff;
  static constexpr size_t overhead_per_byte = sizeof(Value);
  static constexpr size_t vmem_shadow_granularity = Granularity;
  static constexpr size_t bytes_per_entry =
      overhead_per_byte + vmem_shadow_granularity;
  static constexpr size_t vmem_user_addressable_bytes =
      vmem_bytes / bytes_per_entry * vmem_shadow_granularity;
  static constexpr size_t vmem_shadow_size =
      vmem_bytes - vmem_user_addressable_bytes;

  shadowmem_reservevm() {
    shadowmem = (uintptr_t) mmap(nullptr, vmem_shadow_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if ((void*)shadowmem == MAP_FAILED) {
      perror("SHADOW MEM");
      _exit(1);
    }
    shadowmem_end = shadowmem + vmem_shadow_size;
    // outs_red << "Want mem size: " << vmem_shadow_size << " = 2^"
    //          << log2(vmem_shadow_size) << std::endl;
    // outs_red << "Class size (bytes): " << sizeof(os_label) << std::endl;
  }

  __attribute__((always_inline))
  inline Value& addr_to_shadow(uintptr_t addr) const {
#ifdef TRACE_CALLS
    outs_red << "addr_to_shadow(" << std::hex << addr << ")" << std::endl;
#endif
    assert((size_t)addr < vmem_bytes && "VMEM BYTES ASSUMPTION VIOLATION");

    // We have to be piecewise. Project the addresses above our shadow memory as
    // if they were glued onto the lower addresses below our shadow memory.
    assert((addr < shadowmem || addr >= shadowmem_end) && "Addr already within vmem shadow?!?");
    if (__builtin_expect(addr >= shadowmem_end, 0)) {
      addr -= vmem_shadow_size;
    }

#ifdef TRACE_CALLS
    outs_red << "addr = " << std::hex << addr << std::endl;
#endif

    // Now we have to re-scale our address onto our shadow mapping.
    // Fortunately, since each granule/byte indexes into the array, we can just...
    // index.
    Value *shadow_addr =
        &((Value *)shadowmem)[addr / vmem_shadow_granularity];

    assert((uintptr_t)shadow_addr >= shadowmem &&
           (uintptr_t)shadow_addr < shadowmem_end && "Shadow addr projected OOB!");

    return *shadow_addr;
  }

  template<typename Fn>
  __attribute__((always_inline))
  inline void for_each(uintptr_t beg, uintptr_t end, Fn&& fn) {
    size_t num_bytes = end - beg;
    Value *labels = &addr_to_shadow(beg);
    size_t num_granules = (num_bytes + vmem_shadow_granularity - 1) / vmem_shadow_granularity;
    #pragma unroll 2
    for (size_t i = 0; i < num_granules; ++i) {
      fn(beg + i * vmem_shadow_granularity, labels[i]);
    }
  }
};

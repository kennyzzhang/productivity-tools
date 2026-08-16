#include <cassert>
#include <cstdint>
#include <iostream>
#include <typeinfo>

using size_t = std::size_t;

#include "shadowmem.h"

template<typename Value>
class lazy_alloc_ptr {
  Value* ptr = nullptr;

public:
  ~lazy_alloc_ptr() {
    if (ptr != nullptr) delete ptr;
  }

  Value& get() {
    return *(ptr != nullptr ? ptr : ptr = new Value);
  }
};

template<typename Value, int... Widths>
class page_table;

template<typename Value, int Width>
class page_table<Value, Width> {
  Value table[1 << Width] = {};

public:
  operator Value*() {
    return table;
  }
};

template<typename Value, int Width, int... NextWidths>
class page_table<Value, Width, NextWidths...> {
  lazy_alloc_ptr<page_table<Value, NextWidths...>> table[1 << Width] = {};

public:
  Value& operator[](uintptr_t idx) {
    constexpr int nextlevels = (NextWidths + ...);
    size_t tableidx = idx >> nextlevels;
    assert(tableidx >> Width == 0 && "Table index out of bounds") ;

    size_t nextidx = idx & (((uintptr_t)1 << nextlevels) - 1);
    return table[tableidx].get()[nextidx];
  }
};

template<typename Value, size_t Granularity, int... Widths>
class shadowmem_pagetable : shadowmem<shadowmem_pagetable<Value, Granularity, Widths...>> {
  using base = shadowmem<shadowmem_pagetable<Value, Granularity, Widths...>>;
  page_table<Value, Widths...> pt;

public:
  static constexpr uintptr_t vmem_shadow_granularity = Granularity;

  shadowmem_pagetable() {
    int total_width = (Widths + ...);
    if (vmem_shadow_granularity << total_width < (uintptr_t)1 << 48) {
      std::clog << "Warning: total page table width (0";
      ((std::clog << " + " << Widths) , ...);
      std::clog << " = " << total_width
        << ") with granularity " << vmem_shadow_granularity
        << "is less than assumed 48-bit pointer width." << std::endl;
    }
  }

  template<typename Fn>
  void for_each(uintptr_t beg, uintptr_t end, Fn fn) {
    beg = base::floordivgrain(beg);
    end = base::ceildivgrain(end);
    for (uintptr_t i = beg; i != end; i++) {
      // TODO: smarter walking of page table
      fn(i, pt[i]);
    }
  }
};

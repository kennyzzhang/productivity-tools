#pragma once

template<typename Impl>
class shadowmem {
  static constexpr size_t vmem_shadow_granularity = Impl::vmem_shadow_granularity;

protected:
  shadowmem() = default;

public:
  static constexpr uintptr_t floordivgrain(uintptr_t addr) {
    return addr / vmem_shadow_granularity;
  }

  static constexpr uintptr_t ceildivgrain(uintptr_t addr) {
    return (addr + vmem_shadow_granularity - 1) / vmem_shadow_granularity;
  }

  struct coerce_ptr {
    uintptr_t val;
    coerce_ptr(void* ptr) : val((uintptr_t) ptr) {}
    operator uintptr_t() const { return val; }
  };


  template<typename Fn>
  inline void for_each(coerce_ptr beg, coerce_ptr end, Fn&& fn) {
    static_cast<Impl*>(this)->for_each(beg, end, std::forward<Fn>(fn));
  }
};


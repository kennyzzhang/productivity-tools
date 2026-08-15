#pragma once

template<typename Impl>
class shadowmem {
  struct coerce_ptr {
    uintptr_t val;
    coerce_ptr(void* ptr) : val((uintptr_t) ptr) {}
    operator uintptr_t() const { return val; }
  };

protected:
  shadowmem() = default;

public:
  template<typename Fn>
  void for_each(coerce_ptr beg, coerce_ptr end, Fn fn) { static_cast<Impl*>(this)->for_each(beg, end, fn); }
};


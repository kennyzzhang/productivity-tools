/// -*- C++ -*-
#pragma once

#include <unordered_set>
#include <vector>
#include <cassert>
#include <cilk/cilk.h>

using set_t = std::unordered_set<uint64_t>;

// Type for a shadow stack frame
struct shadow_stack_frame_t {
  set_t sr;
  set_t sw;
  set_t pr;
  set_t pw;
};

std::ostream& operator<<(std::ostream& os, set_t s) {
  for (auto x : s)
    os << (void*)x << ", ";
  return os;
}

bool is_disjoint(set_t& small, set_t& large)
{
  if (small.size() > large.size()) // Small into large merging
    return is_disjoint(large, small);
  for (auto access : small)
    if (large.count(access))
      return false;
  return true;
}

//Merges the second argument into the first. Potentially modifies the second argument
void merge_into(set_t& large, set_t& small) 
{
  if (small.size() > large.size()) // Small into large merging
    std::swap(small, large);
  
  for (auto access : small)
    large.insert(access);
}

// Type for a shadow stack
struct shadow_stack_t {
private:
  // Dynamic array of shadow-stack frames.
  std::vector<shadow_stack_frame_t> frames;

public:
  shadow_stack_t() : frames(1) {
  }
  
  shadow_stack_t(const shadow_stack_t &oth) : frames(oth.frames) {
  }

  shadow_stack_frame_t push() {
    frames.emplace_back();
    return frames.back();
  }

  shadow_stack_frame_t pop() {
    assert(!frames.empty() && "Trying to pop() from empty shadow stack!");
    auto ret = frames.back();
    frames.pop_back();
    return ret;
  }

  shadow_stack_frame_t& back() {
    assert(!frames.empty() && "Trying to back() from empty shadow stack!");
    return frames.back();
  }

  /// Reducer support

  static void identity(void *view) {
    assert(false);
    new (view) shadow_stack_t();
  }

  static void reduce(void *left_view, void *right_view) {
    shadow_stack_t *left = static_cast<shadow_stack_t *>(left_view);
    shadow_stack_t *right = static_cast<shadow_stack_t *>(right_view);

    assert(1 == right->frames.size());

#if TRACE_CALLS
    fprintf(stderr, "Reducing ");
#endif

    right->~shadow_stack_t();
  }
};

typedef shadow_stack_t cilk_reducer(shadow_stack_t::identity,
                                    shadow_stack_t::reduce)
  shadow_stack_reducer;



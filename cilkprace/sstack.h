/// -*- C++ -*-
#pragma once

#include <unordered_set>
#include <vector>
#include <cassert>
#include <iostream>
#include <cilk/cilk.h>

using set_t = std::unordered_set<uint64_t>;

// Type for a shadow stack frame
// Accesses are stored in the serial section
// The Parallel section stores dead task's parallel writes
struct shadow_stack_frame_t {
  bool has_children = false;
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
  ~shadow_stack_t() {
    assert(frames.size() <= 1 && "Shadow stack destructed with information!");
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

  // Dumps the parallel section of the current stack frame into the serial section
  // and clears the has_children flag
  // Intended to be used during a sync
  void enter_serial() {
    merge_into(back().sw, back().pw);
    back().pw.clear();
    back().has_children = false;
  } 
  
  // Merges oth with the current stack frame as if they occurred in parallel.
  // Returns true if the two stack frames are disjoint
  bool attach(shadow_stack_frame_t& oth) {
    bool disjoint = is_disjoint(oth.sw, back().pw);
    merge_into(back().pw, oth.sw);

    return disjoint;
  }

  // Declare that the current stack frame has children and create a stack frame for the child
  void before_detach() {
    back().has_children = true;
    push();
  }
  
  // Registers a write to the current frame
  void register_write(uint64_t addr) {
    if (back().has_children)
      back().pw.insert(addr);    
    else
      back().sw.insert(addr);
  }

  /// Reducer support

  static void identity(void *view) {
    new (view) shadow_stack_t();
  }

  static void reduce(void *left_view, void *right_view) {
    shadow_stack_t *left = static_cast<shadow_stack_t *>(left_view);
    shadow_stack_t *right = static_cast<shadow_stack_t *>(right_view);


#if TRACE_CALLS
    auto wnum = __cilkrts_get_worker_number();
    std::cerr << "[" << wnum << "] Reducing " << std::endl;
    std::cerr << "right->back().pw: " << right->back().pw << std::endl;
    std::cerr << "right->back().sw: " <<  right->back().sw << std::endl;
#endif
    assert(1 == right->frames.size());

    //Invariant: steals and reducing shouldn't change the stack's state at the end    
    // OR what checks are done for races
    // This is like a sync
    // BUT it means that task exits have used incomplete information
    // So it's like a soft task exit
  
    // Pretend this is a sync
    merge_into(right->back().sw, right->back().pw);
    right->back().pw.clear();
  
    // Soft Task Exit
    bool disjoint = is_disjoint(left->back().pw, right->back().sw);
    if (!disjoint) 
    {
      std::cerr << "\n\nRACE CONDITION (reducer)" << std::endl;
      std::cerr << "left->back().pw: " << left->back().pw << std::endl;
      std::cerr << "right->back().sw: " <<  right->back().sw << std::endl;
      std::cerr << "RACE CONDITION (reducer)\n\n" << std::endl;
    }

    merge_into(left->back().pw, right->back().sw);
    right->~shadow_stack_t();
  }
};

typedef shadow_stack_t cilk_reducer(shadow_stack_t::identity,
                                    shadow_stack_t::reduce)
  shadow_stack_reducer;



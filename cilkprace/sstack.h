/// -*- C++ -*-
#pragma once

#include <unordered_set>
#include <algorithm>
#include <vector>
#include <cassert>
#include <iostream>
#include <cilk/cilk.h>

#include "outs_red.h"

using set_t = std::unordered_set<uint64_t>;

// Type for a shadow stack frame
// A frame represents serial work followed by parallel work
// serial vs parlalel determines whether or not disjointness checks are made
struct shadow_stack_frame_t {
  bool is_continue = false;
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

bool is_disjoint(set_t& small, set_t& large, set_t& intersect)
{
  #ifdef TRACE_CALLS
  outs_red << "disjoint 1 \t" << small << std::endl << "disjoint 2 \t" << large << std::endl;
  #endif
  if (small.size() > large.size()) // Small into large merging
    return is_disjoint(large, small, intersect);
  for (auto access : small)
    if (large.count(access))
      intersect.insert(access);
  return intersect.empty();
}

//Merges the second argument into the first. Potentially modifies the second argument
void merge_into(set_t& large, set_t& small) 
{
  if (small.size() > large.size()) // Small into large merging
    std::swap(small, large);
  
  #ifdef TRACE_CALLS
  outs_red << "merge " << small << std::endl << "into " << large << std::endl;
  #endif
  
  for (auto access : small)
    large.insert(access);
}

// Type for a shadow stack
struct shadow_stack_t {
private:
  // Dynamic array of shadow-stack frames.
  std::vector<shadow_stack_frame_t> frames;

public:
  shadow_stack_t(const int num=1) : frames(num) {
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
  // Intended to be used during a sync
  bool enter_serial(set_t& collisions) {
#ifdef TRACE_CALLS
    outs_red << "enter_serial with " << frames.size() << " frames!" << std::endl;
#endif

    // The state of the stack at this point is a bit funky
    // We know that all forks have joined
    while(frames.size() >= 2 && back().is_continue)
    {
      auto oth = pop();
      // The other stack contains its accesses in the serial set and parallel set
      // Now it's only serial set
      merge_into(oth.sw, oth.pw);

      // Check if there's a race 
      is_disjoint(back().pw, oth.sw, collisions);
        
      merge_into(back().pw, oth.sw);
    }
  
    // Merge these tasks into serial
    merge_into(back().sw, back().pw);
    back().pw.clear();
    return collisions.empty();
  } 
  
  // Merges oth with the current stack frame as if they occurred in parallel.
  // Returns true if the two stack frames are disjoint
  bool join(set_t& collisions) {
#ifdef TRACE_CALLS
    outs_red << "join" << std::endl;
#endif
    // Grab that fork's frame
    auto oth = pop();
    assert(oth.is_continue == false && "Expected task frame in join!");

    // The other stack contains its accesses in the serial set and parallel set
    // Now it's only serial set
    merge_into(oth.sw, oth.pw);

    // Check if there's a race 
    is_disjoint(back().pw, oth.sw, collisions);

    // We have to store those writes 
    merge_into(back().pw, oth.sw);
    
    return collisions.empty();
  }

  // Declare that the current stack frame has children and create a stack frame for the child
  void add_task_frame() {
#ifdef TRACE_CALLS
    outs_red << "add_task_frame" << std::endl;
#endif
    push(); 
    back().is_continue = false;
  }
    
  void add_continue_frame() {
#ifdef TRACE_CALLS
    outs_red << "add_continue_frame" << std::endl;
#endif
    push(); 
    back().is_continue = true;
  }
  
  // Registers a write to the current frame
  void register_write(uint64_t addr) {
#ifdef TRACE_CALLS
    outs_red << "register_write on " << (void*)addr << std::endl;
#endif
    back().sw.insert(addr);
  }
  
  /// Reducer support
  void append_stack(shadow_stack_t& oth) {
#if TRACE_CALLS
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    auto wnum = __cilkrts_get_worker_number();
    #pragma clang diagnostic pop
    std::cerr << "[" << wnum << "] Append Stack" << std::endl;
#endif
    frames.insert(frames.end(), oth.frames.begin(), oth.frames.end());
  }

  static void identity(void *view) {
#if TRACE_CALLS
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    auto wnum = __cilkrts_get_worker_number();
    #pragma clang diagnostic pop
    std::cerr << "[" << wnum << "] Identity" << std::endl;
#endif

    new (view) shadow_stack_t(0);
  }

  static void reduce(void *left_view, void *right_view) {
    shadow_stack_t *left = static_cast<shadow_stack_t *>(left_view);
    shadow_stack_t *right = static_cast<shadow_stack_t *>(right_view);


#if TRACE_CALLS
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    auto wnum = __cilkrts_get_worker_number();
    #pragma clang diagnostic pop
    std::cerr << "[" << wnum << "] Reducing " << std::endl;
    std::cerr << "left->frames.size(): " << left->frames.size() << std::endl;
    std::cerr << "right->frames.size(): " << right->frames.size() << std::endl;
    //std::cerr << "right->back().pw: " << right->back().pw << std::endl;
    //std::cerr << "right->back().sw: " <<  right->back().sw << std::endl;
#endif
    assert(right && "Reducer given NULL pointer????");
    left->append_stack(*right);

    right->~shadow_stack_t();
  }
};

typedef shadow_stack_t cilk_reducer(shadow_stack_t::identity,
                                    shadow_stack_t::reduce)
  shadow_stack_reducer;



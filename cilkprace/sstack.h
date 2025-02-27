/// -*- C++ -*-
#pragma once

#include <unordered_set>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <vector>
#include <cassert>
#include <iostream>
#include <cilk/cilk.h>
#include <variant>

#include "outs_red.h"

#define TRACE_CALLS 1
#undef TRACE_CALLS

// helper type for the visitor #4
template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
// explicit deduction guide (not needed as of C++20)
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

using set_t = std::unordered_set<uint64_t>;
using map_t = std::unordered_map<uint64_t, source_loc_t>;
using multimap_t = std::unordered_multimap<uint64_t, source_loc_t>;

/*
sp frames represent series work followed by parallel work
That is, the s set represents the serial work
and the p set represents the union of the parallel work's sets

We use a binary forking model with unbounded joining (syncs)

Tasks and Continues are two types of parallel work
cilk_sync forces all parallel work spawned in the current function to complete before we can proceed

In other words, every function is a sync border
Equally, every function that may_spawn must serve as a sync border

Continues can be stolen! We don't necessarily know if they're stolen?

A sync must collect all parallel work that preceedes it in a function

Boundary frames are a natural and correct way to do this
Boundary frames are easy to prove correctness for under reducers.
Much like the ostream reducer, the stack is concatenated back in the correct order


Stack allocations depend on real ordering. Logical parallel analysis doesn't work.
Whenever your deallocate a part of the stack, it might get reallocated logically in parallel but really in serial.

You have unmark any deallocated stack by the time the disjoint check runs / it's 
dumped into the p set

That is, on every stack deallocation (prob func exit), we have to track what is deallocated.
We don't care about how the stack pointer moves within a serial peice of code, we only care about it's current value and high water mark. That is, what can be reallocated.
We lose use-after-free, but that's fine. asan catches that.
Because we sync before task exit, we only check serial

We only need one fn info frame per task and continue :)

tricky case: alloca during continue

*/


// Type for a serial-parallel frame
// stores stack allocation determinism fixing info
struct stack_tracker_t {
  // Range of memory automatically freed by SP
  const void* low_sp = nullptr;
  const void* high_sp = nullptr;

  void register_alloca(const void* addr, uint64_t nb)
  {
    const void* low_alloc = addr;
    const void* high_alloc = ((const char*)addr)+nb;
#ifdef TRACE_CALLS
    outs_red << "register_alloca from " << low_alloc << " to " << high_alloc << std::endl;
#endif
    if (!high_sp)
    {
      high_sp = high_alloc;
      low_sp = low_alloc;
      return;
    }
    
    //Allocas can be reordered on -O3. This assert is not useful or true
    //assert(high_sp >= old_sp && "Stack grew in unexpected direction!");
    
    auto new_low_sp = std::min(low_sp, low_alloc);
    auto new_high_sp = std::max(high_sp, high_alloc);
    
    auto sp_jump = std::max((const char*)(low_sp) - (const char*)(new_low_sp), (const char*)(new_high_sp) - (const char*)(high_sp));
    if (sp_jump > 1024*1024) // 1 MiB
    {   
//#ifdef TRACE_CALLS
    outs_red << "\n\tWARNING unexpected SP move! " << sp_jump <<  " bytes\n" << std::endl;
//#endif
    }
    low_sp = new_low_sp;
    high_sp = new_high_sp;
  }
};  

// A frame represents serial work followed by parallel work
// serial vs parallel determines whether or not disjointness checks are made
struct sp_frame_t {
  map_t sr;
  map_t sw;
  map_t pr;
  map_t pw;
  stack_tracker_t stack_info;

  void unregister_deallocated_stack()
  {
#ifdef TRACE_CALLS
      outs_red << "unregister_deallocated_stack()" << std::endl;
#endif
    //Assumption: They allocated the whole memory, so they're going to use the whole memory 
    //TODO: consider incrmenting by more than 1. See register read and write
    for (uint64_t addr = (uint64_t)stack_info.low_sp; addr <= (uint64_t)stack_info.high_sp; addr += 1)
    {
      sw.erase(addr);
      sr.erase(addr);
    }
  }
};


// sp frame that represents a task
struct task_t : public sp_frame_t {
  const csi_id_t task_id;

  task_t(const csi_id_t task_id) : task_id(task_id){};
};

// sp frame the represents a continue
struct continue_t : public sp_frame_t {

};
// non-sp frame that represents the sync border
struct boundary_t {
  const csi_id_t func_id;
  const int nonboundary_lookback;
  boundary_t(const csi_id_t func_id, const int lookback) : func_id(func_id), nonboundary_lookback(lookback) {};
};

// Union type that represents a task, continue, or boundary
using frame_t = std::variant<boundary_t, continue_t, task_t>;


inline std::ostream& operator<<(std::ostream& os, set_t s) {
  for (auto x : s)
    os << (void*)x << ", ";
  return os;
}

inline std::ostream& operator<<(std::ostream& os, source_loc_t s) {
  os << "(" << s.name << ", " << s.line_number<< ")";
  return os;
}

inline std::ostream& operator<<(std::ostream& os, map_t s) {
  for (auto it : s)
    os << (void*)it.first << ", ";
  return os;
}
inline std::ostream& operator<<(std::ostream& os, multimap_t s) {
  for (auto it = s.begin(); it != s.end();)
  {
    auto current = it->first; 
    os << (void*) it->first << ": " << it->second;
    while((++it) != s.end() && it->first == current)
      os << ", " << it->second;
    os << std::endl;
  }
  return os;
}

// Places the intersection of two maps in intersect and returns true if the intersection is empty
inline bool is_disjoint(map_t& small, map_t& large, multimap_t& intersect)
{
  #ifdef TRACE_CALLS
  outs_red << "disjoint 1 \t" << small << std::endl << "disjoint 2 \t" << large << std::endl;
  #endif
  if (small.size() > large.size()) // Small into large merging
    return is_disjoint(large, small, intersect);
  for (auto access : small)
    if (large.count(access.first))
    {
      auto other_access = large.find(access.first);
      intersect.emplace(access.first, access.second);
      intersect.emplace(other_access->first, other_access->second);
    }
  return intersect.empty();
}

//Merges the second argument into the first. Potentially modifies the second argument
inline void merge_into(map_t& large, map_t& small) 
{
  if (small.size() > large.size()) // Small into large merging
    std::swap(small, large);
  
  #ifdef TRACE_CALLS
  outs_red << "merge " << small << std::endl << "into " << large << std::endl;
  #endif
  
  for (auto access : small)
    large[access.first] = access.second;
    //large.insert(access);
}

// Type for a shadow stack
struct shadow_stack_t {
private:
  // Dynamic array of shadow-stack frames.
  std::vector<frame_t> frames;

public:
  shadow_stack_t(bool has_frame=true) {
    if (has_frame)
    {
      push_task(-1);
    }
  }

  ~shadow_stack_t() {
    #ifdef TRACE_CALLS
    std::cerr << "DESTRUCTING: " << frames.size() << std::endl;
    #endif
    assert(frames.size() <= 1 && "Shadow sp stack destructed with information!");
  }
  
  shadow_stack_t(const shadow_stack_t &oth) : frames(oth.frames) {
  }

  task_t push_task(const csi_id_t task_id) {
    #ifdef TRACE_CALLS
    outs_red << "+task" << std::endl;
    #endif
    frames.emplace_back(std::in_place_type<task_t>, task_id);
    return std::get<task_t>(frames.back());
  }
  boundary_t push_boundary(const csi_id_t func_id) {
    #ifdef TRACE_CALLS
    outs_red << "+boundary" << std::endl;
    #endif
    int lookback = 1;
    if (!frames.empty() && std::holds_alternative<boundary_t>(back()))
      lookback += std::get<boundary_t>(back()).nonboundary_lookback;

    frames.emplace_back(std::in_place_type<boundary_t>, func_id, lookback);
    return std::get<boundary_t>(frames.back());
  }
  continue_t push_continue() {
    #ifdef TRACE_CALLS
    outs_red << "+continue" << std::endl;
    #endif
    frames.emplace_back(std::in_place_type<continue_t>);
    return std::get<continue_t>(frames.back());
  }

  frame_t pop() {
    #ifdef TRACE_CALLS
    outs_red << "-frame" << std::endl;
    #endif
    assert(!frames.empty() && "Trying to pop() from empty shadow sp stack!");
    auto ret = frames.back();
    frames.pop_back();
    return ret;
  }

  void pop_boundary(const csi_id_t func_id) {
   assert(std::holds_alternative<boundary_t>(back()) && "Expected boundary frame in may_spawn function exit!");
   assert(std::get<boundary_t>(back()).func_id == func_id && "Expected matching function id for boundary frame on function exit");
    pop();
  }

  frame_t& back() {
    assert(!frames.empty() && "Trying to back() from empty shadow sp stack!");
    return frames.back();
  }

  sp_frame_t& backmost_nonboundary() {
    //FIXME: We might have to make this more robust when it comes to merging
    //FIXME: We have to make this more robust
    assert(!frames.empty() && "Trying to back() from empty shadow sp stack!");
  
    long look = frames.size()-1;    
    while(look >= 0 && std::holds_alternative<boundary_t>(frames[look]))
    {
      look -= std::get<boundary_t>(frames[look]).nonboundary_lookback;
    }
    assert(look >= 0 && "Negative backmost_nonboundary frame index!");
    
    assert(!std::holds_alternative<boundary_t>(frames[look]) && "No backmost_nonboundary frame!");

    if (std::holds_alternative<task_t>(frames[look]))
       return std::get<task_t>(frames[look]);
    return std::get<continue_t>(frames[look]);
  }

  // Move into join
  void exit_func(const csi_id_t func_id) {
    /*assert(info().func_id == func_id && "Trying to exit_func() with mismatched func_id!");
     
#ifdef TRACE_CALLS
    outs_red << "clearing from " << info().low_mark << " to " << info().init_sp << std::endl;
#endif
    if (false && info().init_sp)
      for (uint64_t low = (uint64_t)info().low_mark; low <= (uint64_t)info().init_sp; low++)
      {
        back().sw.erase(low);
      }
    
    infos.pop_back();
    */
  }

  // Dumps the parallel section of the current stack frame into the serial section
  // Intended to be used during a sync
  bool enter_serial(multimap_t& collisions) {
#ifdef TRACE_CALLS
    outs_red << "enter_serial with " << frames.size() << " frames!" << std::endl;
#endif

    // The state of the stack at this point is a bit funky
    // We know that all forks have joined
    
    // Accumulate all of the children into the parallel sets 
    while(frames.size() >= 2 && std::holds_alternative<continue_t>(back()))
    {
      continue_t oth = std::get<continue_t>(pop());
      // The other stack contains its accesses in the serial set and parallel set
      auto& into = backmost_nonboundary();
      // All accesses should be in the serial set
      merge_into(oth.sw, oth.pw);
      merge_into(oth.sr, oth.pr);
    
      //Fixup oth.sw using stack info from oth
      oth.unregister_deallocated_stack();

      // Check if there's a race 
      //FIXME: This would be a great place to do the race reporting
      is_disjoint(into.pw, oth.sw, collisions);
      is_disjoint(into.pw, oth.sr, collisions);
      is_disjoint(into.pr, oth.sw, collisions);

      merge_into(into.pw, oth.sw);
      merge_into(into.pr, oth.sr);
    }

#ifdef TRACE_CALLS
    if (frames.size() == 1)
      outs_red << "WARNING hit back of frames on enter_serial!" << std::endl;
#endif

    // We've accumulated our children. We know about all the races or lack thereof. Now we pretend it was all in serial
    auto& into = backmost_nonboundary();
    merge_into(into.sw, into.pw);
    merge_into(into.sr, into.pr);
    into.pw.clear();
    into.pr.clear();
  
    return collisions.empty();
  } 

  // Merges oth with the current stack frame as if they occurred in parallel.
  // Returns true if the two stack frames are disjoint
  bool join(multimap_t& collisions) {
#ifdef TRACE_CALLS
    outs_red << "join: " << frames.size() << std::endl;
#endif
    // Grab that fork's frame
    assert(std::holds_alternative<task_t>(back()) && "Expected task frame in join!");
    task_t oth = std::get<task_t>(pop());
    sp_frame_t& back = backmost_nonboundary();
    
    // The other stack contains its accesses in the serial set and parallel set
    // Now it's only serial set
    merge_into(oth.sw, oth.pw);
    merge_into(oth.sr, oth.pr);

    //Fixup oth.sw and oth.sr using stack info from oth
    oth.unregister_deallocated_stack();

    // Check if there's a race 
    //TODO: This would be a great place to do the race reporting
    is_disjoint(back.pw, oth.sw, collisions);
    is_disjoint(back.pw, oth.sr, collisions);
    is_disjoint(back.pr, oth.sw, collisions);

    // We have to store those writes 
    merge_into(back.pw, oth.sw);
    merge_into(back.pr, oth.sr);
    
    return collisions.empty();
  }

  // Registers a write to the current frame
  void register_write(uint64_t addr, size_t num_bytes, source_loc_t store) {
#ifdef TRACE_CALLS
    outs_red << "register_write on " << (void*)addr << std::endl;
#endif
    backmost_nonboundary().sw[addr] = store;
  }
  
  // Registers a read to the current frame
  void register_read(uint64_t addr, size_t num_bytes, source_loc_t store) {
#ifdef TRACE_CALLS
    outs_red << "register_read on " << (void*)addr << std::endl;
#endif
    backmost_nonboundary().sr[addr] = store;
  }
  
  
  // Registers an alloca to the current frame
  void register_alloca(const void* addr, size_t nb) {
#ifdef TRACE_CALLS
    outs_red << "register_alloca on " << addr << ", " << nb << std::endl;
#endif
    backmost_nonboundary().stack_info.register_alloca(addr, nb);
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
    //frames.reserve(frames.size() + oth.frames.size());
    std::move(oth.frames.begin(), oth.frames.end(), std::back_inserter(frames));
    oth.frames.clear();
  }

  static void identity(void *view) {
#if TRACE_CALLS
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    auto wnum = __cilkrts_get_worker_number();
    #pragma clang diagnostic pop
    std::cerr << "[" << wnum << "] Identity" << std::endl;
#endif

    new (view) shadow_stack_t(false);
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



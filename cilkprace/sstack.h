/// -*- C++ -*-
#pragma once

#include <unordered_set>
#include <map>
#include <algorithm>
#include <vector>
#include <cassert>
#include <iostream>
#include <cilk/cilk.h>
#include <variant>

#include "outs_red.h"

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
// A frame represents serial work followed by parallel work
// serial vs parallel determines whether or not disjointness checks are made
struct sp_frame_t {
  map_t sr;
  map_t sw;
  map_t pr;
  map_t pw;
};

// stores stack allocation determinism fixing info
struct stack_tracker_t {
  // Range of memory automatically freed by SP
  const void* low_mark = nullptr;
  const void* init_sp = nullptr;

  void register_alloca(const void* addr, uint64_t nb)
  {
    const void* old_sp = ((const char*)addr)+nb;
#ifdef TRACE_CALLS
    outs_red << "register_alloca from " << old_sp << " to " << addr << std::endl;
#endif
    if (!init_sp)
    {
      init_sp = old_sp;
      low_mark = addr;
      return;
    }
    
    if (old_sp != low_mark)
    {   
#ifdef TRACE_CALLS
    outs_red << "\n\tWARNING unexpected SP move! " << (uint64_t)(low_mark) - (uint64_t)old_sp << " bytes\n" << std::endl;
#endif
    }

    assert(init_sp >= old_sp && "Stack grew in unexpected direction!");
    low_mark = std::min(low_mark, addr);
  }
};  

// sp frame that represents a task
struct task_t : public sp_frame_t {
  stack_tracker_t stack_info;
  const csi_id_t task_id;

  task_t(const csi_id_t task_id) : task_id(task_id){};
};

// sp frame the represents a continue
struct continue_t : public sp_frame_t {
  stack_tracker_t stack_info;

};
// non-sp frame that represents the sync border
struct boundary_t {
  const csi_id_t func_id;
  const int nonboundary_lookback;
  boundary_t(const csi_id_t func_id, const int lookback) : func_id(func_id), nonboundary_lookback(lookback) {};
};

// Union type that represents a task, continue, or boundary
using frame_t = std::variant<boundary_t, continue_t, task_t>;
// nonboundary_frame_t
using nb_frame_t = std::variant<continue_t&, task_t&>;


std::ostream& operator<<(std::ostream& os, set_t s) {
  for (auto x : s)
    os << (void*)x << ", ";
  return os;
}

std::ostream& operator<<(std::ostream& os, source_loc_t s) {
  os << "(" << s.name << ", " << s.line_number<< ")";
  return os;
}

std::ostream& operator<<(std::ostream& os, map_t s) {
  for (auto it : s)
    os << (void*)it.first << ", ";
  return os;
}
std::ostream& operator<<(std::ostream& os, multimap_t s) {
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
bool is_disjoint(map_t& small, map_t& large, multimap_t& intersect)
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
void merge_into(map_t& large, map_t& small) 
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
      push_boundary(-1);
      //push_continue();
    }
  }

  ~shadow_stack_t() {
    std::cerr << "DESTRUCTING: " << frames.size() << std::endl;
    assert(frames.size() <= 1 && "Shadow sp stack destructed with information!");
  }
  
  shadow_stack_t(const shadow_stack_t &oth) : frames(oth.frames) {
  }

  task_t push_task(const csi_id_t task_id) {
    frames.emplace_back(std::in_place_type<task_t>, task_id);
    return std::get<task_t>(frames.back());
  }
  boundary_t push_boundary(const csi_id_t func_id) {
    int lookback = 1;
    if (std::holds_alternative<boundary_t>(back()))
      lookback += std::get<boundary_t>(back()).nonboundary_lookback;

    frames.emplace_back(std::in_place_type<boundary_t>, func_id, lookback);
    return std::get<boundary_t>(frames.back());
  }
  continue_t push_continue() {
    frames.emplace_back(std::in_place_type<continue_t>);
    return std::get<continue_t>(frames.back());
  }

  frame_t pop() {
    assert(!frames.empty() && "Trying to pop() from empty shadow sp stack!");
    auto ret = frames.back();
    frames.pop_back();
    return ret;
  }

  void pop_boundary(const csi_id_t func_id) {
   assert(std::holds_alternative<boundary_t>(back()) && "Expected boundary frame in may_spawn function exit!");
   assert(std::get<boundary_t>(pop()).func_id == func_id && "Expected matching function id for boundary frame on function exit");
  }

  frame_t& back() {
    assert(!frames.empty() && "Trying to back() from empty shadow sp stack!");
    return frames.back();
  }

  nb_frame_t backmost_nonboundary() {
    //FIXME: We might have to make this more robust when it comes to merging
    //FIXME: We have to make this more robust
    assert(!frames.empty() && "Trying to back() from empty shadow sp stack!");
  
    long look = frames.size()-1;    
    while(look >= 0 && std::holds_alternative<boundary_t>(frames[look]))
    {
      look -= std::get<boundary_t>(frames[look]).nonboundary_lookback;
    }
    assert(look >= 0 && "Negative backmost_nonboundary frame index!");
    
    return std::visit(overloaded{ 
      [&](boundary_t& into) -> nb_frame_t {
        assert(false && "No backmost_nonboundary frame!");
        return nb_frame_t();
      },
      [&](auto&& into) -> nb_frame_t {
        return into; 
      }       
    }, frames[look]);
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
    
     
    while(frames.size() >= 2 && std::holds_alternative<continue_t>(back()))
    {
      auto oth = pop();
      // The other stack contains its accesses in the serial set and parallel set
      std::visit([&](auto&& into) {
        // All accesses should be in the serial set
        merge_into(oth.sw, oth.pw);
        
        // Check if there's a race 
        is_disjoint(into.pw, oth.sw, collisions);
        
        merge_into(into.pw, oth.sw);
  
      }, backmost_nonboundary());
    }

#ifdef TRACE_CALLS
    if (frames.size() == 1)
      outs_red << "WARNING hit back of frames on enter_serial!" << std::endl;
#endif

    std::visit([&](auto&& into) {
        // Merge these tasks into serial
        merge_into(into.sw, into.pw);
        into.pw.clear();
    }, backmost_nonboundary());
  
    return collisions.empty();
  } 
  
  // Merges oth with the current stack frame as if they occurred in parallel.
  // Returns true if the two stack frames are disjoint
  bool join(multimap_t& collisions) {
#ifdef TRACE_CALLS
    outs_red << "join" << std::endl;
#endif
    // Grab that fork's frame
    assert(std::holds_alternative<task_t>(back()) && "Expected task frame in join!");
    task_t oth = std::get<task_t>(pop());
    nb_frame_t& back = backmost_nonboundary();
    
    std::visit([&collisions, &oth](auto&& back) { 
          
        //TODO: Fixup stack
  
        // The other stack contains its accesses in the serial set and parallel set
        // Now it's only serial set
        merge_into(oth.sw, oth.pw);

        // Check if there's a race 
        is_disjoint(back.pw, oth.sw, collisions);

        // We have to store those writes 
        merge_into(back.pw, oth.sw);

        }, back);
    
    return collisions.empty();
  }

  // Registers a write to the current frame
  void register_write(uint64_t addr, source_loc_t store) {
#ifdef TRACE_CALLS
    outs_red << "register_write on " << (void*)addr << std::endl;
#endif
    std::visit([&](auto&& back) {
      back.sw[addr] = store;
    }, backmost_nonboundary());
  }
  
  // Registers an alloca to the current frame
  void register_alloca(const void* addr, size_t nb) {
#ifdef TRACE_CALLS
    outs_red << "register_alloca on " << addr << ", " << nb << std::endl;
#endif
    std::visit([&](auto&& back) {
        back.stack_info.register_alloca(addr, nb);
    }, backmost_nonboundary());
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



/// -*- C++ -*-
#pragma once

#include <unordered_set>
#include <map>
#include <algorithm>
#include <vector>
#include <cassert>
#include <iostream>
#include <cilk/cilk.h>

#include "outs_red.h"

using set_t = std::unordered_set<uint64_t>;
using map_t = std::unordered_map<uint64_t, source_loc_t>;
using multimap_t = std::unordered_multimap<uint64_t, source_loc_t>;

// Type for a serial-parallel frame
// A frame represents serial work followed by parallel work
// serial vs parallel determines whether or not disjointness checks are made
struct sp_frame_t {
  bool is_continue = false;
  map_t sr;
  map_t sw;
  map_t pr;
  map_t pw;
};

// function stack metadata frame
struct fn_info_frame_t {
  const csi_id_t func_id;
  const void* low_mark = nullptr;
  const void* init_sp = nullptr;

  fn_info_frame_t (const csi_id_t func_id=-1): func_id(func_id) {};

  void register_alloca(const void* addr, uint64_t nb)
  {
    const void* start_addr = ((const char*)addr)+nb;
    if (!init_sp)
    {
      init_sp = start_addr;
      low_mark = addr;
    }
    assert(init_sp >= start_addr && "Stack grew in unexepcted direction!");
    low_mark = std::min(low_mark, addr);
  }
};  

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

bool is_disjoint(map_t& small, map_t& large, multimap_t& intersect)
{
  #ifdef TRACE_CALLS
  //outs_red << "disjoint 1 \t" << small << std::endl << "disjoint 2 \t" << large << std::endl;
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
  //outs_red << "merge " << small << std::endl << "into " << large << std::endl;
  #endif
  
  for (auto access : small)
    large[access.first] = access.second;
    //large.insert(access);
}

// Type for a shadow stack
struct shadow_stack_t {
private:
  // Dynamic array of shadow-stack frames.
  std::vector<sp_frame_t> frames;
  std::vector<fn_info_frame_t> infos;

public:
  shadow_stack_t(bool has_frame=true) : frames((int)has_frame), infos((int)has_frame) {
  }

  ~shadow_stack_t() {
    std::cerr << "DESTRUCTING: " << infos.size() << std::endl;
    assert(frames.size() <= 1 && "Shadow sp stack destructed with information!");
    assert(infos.size() <= 1 && "Shadow info stack destructed with information!");
  }
  
  shadow_stack_t(const shadow_stack_t &oth) : frames(oth.frames), infos(oth.infos) {
  }

  sp_frame_t push() {
    frames.emplace_back();
    return frames.back();
  }

  sp_frame_t pop() {
    assert(!frames.empty() && "Trying to pop() from empty shadow sp stack!");
    auto ret = frames.back();
    frames.pop_back();
    return ret;
  }

  sp_frame_t& back() {
    assert(!frames.empty() && "Trying to back() from empty shadow sp stack!");
    return frames.back();
  }
  
  fn_info_frame_t& info() {
    assert(!infos.empty() && "Trying to back() from empty shadow info stack!");
    return infos.back();
  }
  void enter_func(const csi_id_t func_id) {
    infos.emplace_back(func_id);
    assert(!infos.empty() && "Trying to back() from empty shadow info stack!");
  }
  void exit_func(const csi_id_t func_id) {
    assert(info().func_id == func_id && "Trying to exit_func() with mismatched func_id!");
    assert(back().is_continue == false && "Trying to exit_func() with a continue frame!");
     
#ifdef TRACE_CALLS
    outs_red << "clearing from " << info().low_mark << " to " << info().init_sp << std::endl;
#endif
    if (info().init_sp)
      for (uint64_t low = (uint64_t)info().low_mark; low <= (uint64_t)info().init_sp; low++)
      {
        back().sw.erase(low);
      }
    
    infos.pop_back();
  }

  // Dumps the parallel section of the current stack frame into the serial section
  // Intended to be used during a sync
  bool enter_serial(multimap_t& collisions) {
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

#ifdef TRACE_CALLS
    if (frames.size() == 1)
      outs_red << "WARNING hit back of frames on enter_serial!" << std::endl;
#endif
  
    // Merge these tasks into serial
    merge_into(back().sw, back().pw);
    back().pw.clear();
    return collisions.empty();
  } 
  
  // Merges oth with the current stack frame as if they occurred in parallel.
  // Returns true if the two stack frames are disjoint
  bool join(multimap_t& collisions) {
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
  void register_write(uint64_t addr, source_loc_t store) {
#ifdef TRACE_CALLS
    outs_red << "register_write on " << (void*)addr << std::endl;
#endif
    back().sw[addr] = store;
  }
  
  // Registers an alloca to the current frame
  void register_alloca(const void* addr, size_t nb) {
#ifdef TRACE_CALLS
    outs_red << "register_alloca on " << addr << ", " << nb << std::endl;
#endif
    info().register_alloca(addr, nb);
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



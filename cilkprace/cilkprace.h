#pragma once
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <csi/csi.h>
#include "csan.h"

#include "outs_red.h"
#include "stack.h"

#define TRACE_CALLS 1
#undef TRACE_CALLS

#define CILKTOOL_API extern "C" __attribute__((visibility("default")))
#define CILKSAN_API extern "C" __attribute__((visibility("default")))

extern bool HAS_INIT;


// Stack structures for keeping track of MAAP (May Access Alias in Parallel)
// information inserted by the compiler before a call.
enum class MAAP_t : uint8_t {
  NoAccess = 0,
  Mod = 1,
  Ref = 2,
  ModRef = Mod | Ref,
  NoAlias = 4,
};


using MAAPstack = Stack_t<std::pair<csi_id_t, MAAP_t>>;
using ustack = Stack_t<unsigned>;
using pstack = Stack_t<uint8_t>;

  // Stack structures for keeping track of MAAPs for pointer arguments to function calls
  static void init_MAAPstack (void* view) {
#if TRACE_CALLS
    std::cerr << "init MAAPSTACK" << std::endl;
#endif
    new (view) MAAPstack();
  }

  static void reduce_MAAPstack (void* left_view, void* right_view) {
#if TRACE_CALLS
    std::cerr << "reduce MAAPSTACK" << std::endl;
#endif
    MAAPstack *left = static_cast<MAAPstack*>(left_view);
    MAAPstack *right = static_cast<MAAPstack*>(right_view);
    assert(right->size() == 0 && "Expected empty MAAPstack!");
    right->~MAAPstack();
  }

  static void init_ustack(void* view) {
#if TRACE_CALLS
    std::cerr << "init ustack" << std::endl;
#endif
    new (view) ustack();
  }
  static void reduce_ustack (void* left_view, void* right_view) {
#if TRACE_CALLS
    std::cerr << "reduce ustack" << std::endl;
#endif
    ustack *left = static_cast<ustack*>(left_view);
    ustack *right = static_cast<ustack*>(right_view);
    assert(right->size() == 0 && "Expected empty ustack!");
    right->~ustack();
  }
  static void init_pstack(void* view) {
#if TRACE_CALLS
    std::cerr << "init pstack" << std::endl;
#endif
    new (view) pstack();
  }
  static void reduce_pstack (void* left_view, void* right_view) {
#if TRACE_CALLS
    std::cerr << "reduce pstack" << std::endl;
#endif
    pstack *left = static_cast<pstack*>(left_view);
    pstack *right = static_cast<pstack*>(right_view);
    assert(right->size() == 0 && "Expected empty pstack!");
    right->~pstack();
  }

typedef MAAPstack cilk_reducer(init_MAAPstack, reduce_MAAPstack) MAAPstack_reducer;
typedef ustack cilk_reducer(init_ustack, reduce_ustack) ustack_reducer;
typedef pstack cilk_reducer(init_pstack, reduce_pstack) pstack_reducer;

extern pstack_reducer parallel_execution;

//FIXME
__attribute__((always_inline)) /*static*/ inline bool is_execution_parallel() {
  return true; //parallel_execution.back();
}

class CilkpraceImpl_t {
public:
  //shadow_stack_t stack;

private:
  //label_reducer stack;
  // Need to manually register reducer
  //
  // > warning: reducer callbacks not implemented for structure members
  // > [-Wcilk-ignored]
  struct {
    template <class T>
    static void reducer_register(T& red) {
      __cilkrts_reducer_register(&red, sizeof(red),
          &std::decay_t<decltype(*&red)>::identity,
          &std::decay_t<decltype(*&red)>::reduce);
    }

    template <class T>
    static void reducer_unregister(T& red) {
      __cilkrts_reducer_unregister(&red);
    }

    struct RAII {
      CilkpraceImpl_t& this_;

      RAII(decltype(this_) this_) : this_(this_) {
#ifndef OUTS_CERR
        reducer_register(outs_red);
#endif
   //     reducer_register(this_.stack);
        //const char* envstr = getenv("CILKSCALE_OUT");
      }

      ~RAII() {
#ifndef OUTS_CERR
        reducer_unregister(outs_red);
#endif
     //   reducer_unregister(this_.stack);
      }
    } raii;
  } register_reducers = {.raii{*this}};

public:
  CilkpraceImpl_t() /*: stack()*/
         // Not only are reducer callbacks not implemented, the hyperobject
         // is not even default constructed unless explicitly constructed.
  {
    HAS_INIT = true;
#ifdef TRACE_CALLS
    outs_red << "HAS INIT" << std::endl;
#endif
    // Note that we start executing the program in series.
    //parallel_execution.push_back(0);
    // Push a default value of 0 onto the MAAP_counts stack, in case this
    // function contains get_MAAP calls.
    //MAAP_counts.push_back(0);
  }

  ~CilkpraceImpl_t() {}

  void register_write(uint64_t addr, size_t num_bytes, source_loc_t store) {
    //outs_red << "WRITE with pedigree " << __cilkrts_get_pedigree().rank << std::endl;
    outs_red << "WRITE with label " << __cilkrts_get_os_label().label << std::endl;
    //outs_red << "WRITE with pedigree " << "[REDACTED]" << std::endl;
  //  stack.register_write(addr, num_bytes, store);
  }
  void register_read(uint64_t addr, size_t num_bytes, source_loc_t store) {
  //  stack.register_read(addr, num_bytes, store);
    //outs_red << "READ  with pedigree " << __cilkrts_get_pedigree().rank << std::endl;
    //outs_red << "READ  with label " << __cilkrts_get_os_label().label << std::endl;
  }
  void register_alloca(const void* addr, size_t nb) {
  //  stack.register_alloca(addr, nb);
  }
  void advance_stack_frame(uint64_t addr) { 
    outs_red << "UNHANDLED STACK ADVANCE" << std::endl;
  }
  void restore_stack(const csi_id_t call_id, uint64_t addr) { 
    outs_red << "UNHANDLED STACK RESTORE" << std::endl;
  }
  void enter_func(const csi_id_t func_id, const bool may_spawn) {
 /*   if (may_spawn)
    {
      stack.push_boundary(func_id);
    }*/
  }
  void exit_func(const csi_id_t func_id, const bool may_spawn) {
   /* if (may_spawn)
    {
      stack.pop_boundary(func_id);
    }*/ 
  }

  void left_child()
  {
    //__cilkrts_get_os_label().label.append_left_child();
  }

  void right_child() {
     //__cilkrts_get_os_label().label.append_right_child();
  } 

  void left_child_join() {
    //__cilkrts_get_os_label().label.join_left_child();
  }

  void task(const csi_id_t task_id) {
    //stack.push_task(task_id);
    //outs_red << "TASK1 with pedigree " << __cilkrts_get_pedigree().rank << std::endl;
    
    //outs_red << "TASK1 with label " << __cilkrts_get_os_label().label << std::endl;
  }
  void exit_task(const csi_id_t task_id) {
    //outs_red << "SYNC1 with label " << __cilkrts_get_os_label().label << std::endl;
    //multimap_t collisions;
    //stack.join(collisions);
    //if (!collisions.empty())
    //  outs_red << "\nRACE CONDITION TASK EXIT" << std::endl /*<< collisions*/ << std::endl << std::endl;
  }
  void add_sp_frame() {
    
    //stack.add_sp_frame();
  }
  void add_continue_frame() {
   
    //stack.push_continue();
  }
  void enter_serial() {
     
    //outs_red << "SYNC2 with pedigree " << __cilkrts_get_pedigree().rank << std::endl;
    outs_red << "SYNC2 with label " << __cilkrts_get_os_label().label << std::endl;
    //multimap_t collisions;
    //stack.enter_serial(collisions);
    //if (!collisions.empty())
    //  outs_red << "\nRACE CONDITION DURING SYNC" << std::endl /*<< collisions*/ << std::endl << std::endl;
  }
};
//FIXME: Hardcoded for now
__attribute__((always_inline)) /*static*/ inline bool should_check() {
  return HAS_INIT;
}

void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal, uintptr_t ptr, size_t len);
void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal, const void*  ptr, size_t len);
void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal, uintptr_t ptr, size_t len);
void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal, const void*  ptr, size_t len);


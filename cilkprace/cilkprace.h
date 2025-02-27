#pragma once
#include <cilk/cilk_api.h>
#include <csi/csi.h>
#include "csan.h"

#include "outs_red.h"
#include "sstack.h"

#define TRACE_CALLS 1
#undef TRACE_CALLS

#define CILKTOOL_API extern "C" __attribute__((visibility("default")))
#define CILKSAN_API extern "C" __attribute__((visibility("default")))

extern bool HAS_INIT;

class CilkpraceImpl_t {
public:
  //shadow_stack_t stack;

private:
  shadow_stack_reducer stack;
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
        reducer_register(this_.stack);
        //const char* envstr = getenv("CILKSCALE_OUT");
      }

      ~RAII() {
#ifndef OUTS_CERR
        reducer_unregister(outs_red);
#endif
        reducer_unregister(this_.stack);
      }
    } raii;
  } register_reducers = {.raii{*this}};

public:
  CilkpraceImpl_t() : stack()
         // Not only are reducer callbacks not implemented, the hyperobject
         // is not even default constructed unless explicitly constructed.
  {
    HAS_INIT = true;
#ifdef TRACE_CALLS
    outs_red << "HAS INIT" << std::endl;
#endif
  }

  ~CilkpraceImpl_t() {}

  void register_write(uint64_t addr, size_t num_bytes, source_loc_t store) {
    stack.register_write(addr, num_bytes, store);
  }
  void register_read(uint64_t addr, size_t num_bytes, source_loc_t store) {
    stack.register_read(addr, num_bytes, store);
  }
  void register_alloca(const void* addr, size_t nb) {
    stack.register_alloca(addr, nb);
  }
  void advance_stack_frame(uint64_t addr) { 
    outs_red << "UNHANDLED STACK ADVANCE" << std::endl;
  }
  void restore_stack(const csi_id_t call_id, uint64_t addr) { 
    outs_red << "UNHANDLED STACK RESTORE" << std::endl;
  }
  void enter_func(const csi_id_t func_id, const bool may_spawn) {
    if (may_spawn)
    {
      stack.push_boundary(func_id);
    }
  }
  void exit_func(const csi_id_t func_id, const bool may_spawn) {
    if (may_spawn)
    {
      stack.pop_boundary(func_id);
    } 
  }
  void task(const csi_id_t task_id) {
    stack.push_task(task_id);
  }
  void exit_task(const csi_id_t task_id) {
    multimap_t collisions;
    stack.join(collisions);
    if (!collisions.empty())
      outs_red << "\nRACE CONDITION TASK EXIT" << std::endl /*<< collisions*/ << std::endl << std::endl;
  }
  void add_sp_frame() {
    //stack.add_sp_frame();
  }
  void add_continue_frame() {
    stack.push_continue();
  }
  void enter_serial() {
    multimap_t collisions;
    stack.enter_serial(collisions);
    if (!collisions.empty())
      outs_red << "\nRACE CONDITION DURING SYNC" << std::endl /*<< collisions*/ << std::endl << std::endl;
  }
};
// Stack structures for keeping track of MAAP (May Access Alias in Parallel)
// information inserted by the compiler before a call.
enum class MAAP_t : uint8_t {
  NoAccess = 0,
  Mod = 1,
  Ref = 2,
  ModRef = Mod | Ref,
  NoAlias = 4,
};

__attribute__((always_inline)) /*static*/ inline bool should_check() {
  return true;
}

void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal, uintptr_t ptr, size_t len);
void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal, const void*  ptr, size_t len);
void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal, uintptr_t ptr, size_t len);
void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal, const void*  ptr, size_t len);



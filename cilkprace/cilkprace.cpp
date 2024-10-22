#include <cilk/cilk_api.h>
#include <cilk/ostream_reducer.h>
#include <csi/csi.h>
#include <fstream>
#include <iostream>
#include <memory>

#define TRACE_CALLS 1

#include "sstack.h"
#include "outs_red.h"

#define CILKTOOL_API extern "C" __attribute__((visibility("default")))

std::unique_ptr<std::ofstream> outf;
#ifndef OUTS_CERR
cilk::ostream_reducer<char> outs_red([]() -> std::basic_ostream<char>& {
            const char* envstr = getenv("CILKSCALE_OUT");
            if (envstr)
            return *(outf = std::make_unique<std::ofstream>(envstr));
            return std::cout;
            }());
#endif
class CilkgraphImpl_t {
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
      CilkgraphImpl_t& this_;

      RAII(decltype(this_) this_) : this_(this_) {
#ifndef OUTS_CERR
        reducer_register(outs_red);
#endif
        reducer_register(this_.stack);
        const char* envstr = getenv("CILKSCALE_OUT");
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
  CilkgraphImpl_t() : stack()
         // Not only are reducer callbacks not implemented, the hyperobject
         // is not even default constructed unless explicitly constructed.
  {}

  ~CilkgraphImpl_t() {}

  void register_write(uint64_t addr) {
    stack.register_write(addr);
  }
  void add_task_frame() {
    stack.add_task_frame();
  }
  void add_continue_frame() {
    stack.add_continue_frame();
  }
  void enter_serial() {
    set_t collisions;
    stack.enter_serial(collisions);
    if (!collisions.empty())
      outs_red << "\nRACE CONDITION DURING SYNC" << std::endl << "on " << collisions << std::endl << std::endl;
  }
  void join() {
    set_t collisions;
    stack.join(collisions);
    if (!collisions.empty())
      outs_red << "\nRACE CONDITION TASK EXIT" << std::endl << "on " << collisions << std::endl << std::endl;
  }

};

static std::unique_ptr<CilkgraphImpl_t> tool =
std::make_unique<decltype(tool)::element_type>();

static unsigned worker_number() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  return __cilkrts_get_worker_number();
#pragma clang diagnostic pop
}

CILKTOOL_API void __csi_init() {
}

CILKTOOL_API void __csi_unit_init(const char* const file_name,
                                  const instrumentation_counts_t counts) {
}

CILKTOOL_API
void __csi_func_entry(const csi_id_t func_id, const func_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] func(fid=" << func_id << ", nsr="
      << prop.num_sync_reg << ")" << std::endl;
#endif
  auto entry = __csi_get_func_source_loc(func_id);
  outs_red << "FUNC: " << entry->name << std::endl;
  // TODO: if we ever get better task hooks, this (and the corresponding call
  // in func_exit) can be removed
  tool->add_task_frame();
}

CILKTOOL_API
void __csi_func_exit(const csi_id_t func_exit_id, const csi_id_t func_id,
                     const func_exit_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] func_exit(feid=" << func_exit_id
      << ", fid=" << func_id << ")" << std::endl;
#endif
  // TODO: if we ever get better task hooks, this (and the corresponding call
  // in func_entry) can be removed
  tool->join();
}

CILKTOOL_API void __csi_before_load(const csi_id_t load_id, const void *addr,
                            const int32_t num_bytes, const load_prop_t prop) {
return;
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_load(lid=" << load_id << ", addr="
      << addr << ", nb=" << num_bytes << ", align=" << prop.alignment
      << ", vtab=" << prop.is_vtable_access << ", const=" << prop.is_constant
      << ", stack=" << prop.is_on_stack << ", cap=" << prop.may_be_captured
      << ", atomic=" << prop.is_atomic << ", threadlocal="
      << prop.is_thread_local << ", basic_read_before_write="
      << prop.is_read_before_write_in_bb << ")" << std::endl;
#endif
}

CILKTOOL_API void __csi_after_load(const csi_id_t load_id, const void *addr,
                           const int32_t num_bytes, const load_prop_t prop) {
return;
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] after_load(lid=" << load_id << ", addr="
      << addr << ", nb=" << num_bytes << ", align=" << prop.alignment
      << ", vtab=" << prop.is_vtable_access << ", const=" << prop.is_constant
      << ", stack=" << prop.is_on_stack << ", cap=" << prop.may_be_captured
      << ", atomic=" << prop.is_atomic << ", threadlocal="
      << prop.is_thread_local << ", basic_read_before_write="
      << prop.is_read_before_write_in_bb << ")" << std::endl;
#endif
}

CILKTOOL_API void __csi_before_store(const csi_id_t store_id, const void *addr,
                             const int32_t num_bytes, const store_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_store(sid=" << store_id
      << ", addr=" << addr << ", nb=" << num_bytes << ", align="
      << prop.alignment << ", vtab=" << prop.is_vtable_access << ", const="
      << prop.is_constant << ", stack=" << prop.is_on_stack << ", cap="
      << prop.may_be_captured << ", atomic=" << prop.is_atomic
      << ", threadlocal=" << prop.is_thread_local << ")" << std::endl;
#endif
  tool->register_write((uint64_t)addr);
  auto store = __csi_get_store_source_loc(store_id);
  outs_red << "WRITE ON " << store->name << std::endl;
}

CILKTOOL_API void __csi_after_store(const csi_id_t store_id, const void *addr,
                            const int32_t num_bytes, const store_prop_t prop) {
return;
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] after_store(sid=" << store_id
      << ", addr=" << addr << ", nb=" << num_bytes << ", align="
      << prop.alignment << ", vtab=" << prop.is_vtable_access << ", const="
      << prop.is_constant << ", stack=" << prop.is_on_stack << ", cap="
      << prop.may_be_captured << ", atomic=" << prop.is_atomic
      << ", threadlocal=" << prop.is_thread_local << ")" << std::endl;
#endif
}

CILKTOOL_API void __csi_task(const csi_id_t task_id, const csi_id_t detach_id,
                             const task_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] task(tid=" << task_id << ", did="
      << detach_id << ", nsr=" << prop.num_sync_reg << ")" << std::endl;
#endif
  tool->add_task_frame();
}

CILKTOOL_API
void __csi_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
                     const csi_id_t detach_id, const unsigned sync_reg,
                     const task_exit_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] task_exit(teid=" << task_exit_id
      << ", tid=" << task_id << ", did=" << detach_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
  // Attach stack as if they occured in parallel.
    tool->join();
//  assert(disjoint && "Race condition!");

}

CILKTOOL_API
void __csi_detach(const csi_id_t detach_id, const unsigned sync_reg,
                  const detach_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] detach(did=" << detach_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_detach_continue(const csi_id_t detach_continue_id,
                           const csi_id_t detach_id, const unsigned sync_reg,
                           const detach_continue_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] detach_continue(dcid="
      << detach_continue_id << ", did=" << detach_id << ", sr=" << sync_reg
      << ", unwind=" << prop.is_unwind << ")" << std::endl;
#endif
  tool->add_continue_frame();
}

CILKTOOL_API
void __csi_before_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_sync(sid=" << sync_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_after_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] after_sync(sid=" << sync_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
  tool->enter_serial();
}

CILKTOOL_API
void __csi_after_alloca(const csi_id_t alloca_id, const void *addr,
                             size_t num_bytes, const alloca_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] after_alloca(aid=" << alloca_id
      << ", addr=" << addr << ", nb=" << num_bytes << ", static="
      << prop.is_static << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_before_allocfn(const csi_id_t allocfn_id, size_t size,
                               size_t num, size_t alignment,
                               const void *oldaddr, const allocfn_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_allocfn(afid=" << allocfn_id
      << ", size=" << size << ", num=" << num << ", align=" << alignment
      << ", oaddr=" << oldaddr << ", type=" << prop.allocfn_ty << ")"
      << std::endl;
#endif
}

CILKTOOL_API
void __csi_after_allocfn(const csi_id_t allocfn_id, const void *addr,
                              size_t size, size_t num, size_t alignment,
                              const void *oldaddr, const allocfn_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] after_allocfn(afid=" << allocfn_id
      << ", addr=" << addr << ", size=" << size << ", num=" << num << ", align="
      << alignment << ", oaddr=" << oldaddr << ", type=" << prop.allocfn_ty
      << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_before_free(const csi_id_t free_id, const void *ptr,
                            const free_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_free(fid=" << free_id
      << ", addr=" << ptr << ", type=" << prop.free_ty << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_after_free(const csi_id_t free_id, const void *ptr,
                           const free_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] after_free(fid=" << free_id
      << ", addr=" << ptr << ", type=" << prop.free_ty << ")" << std::endl;
#endif
}



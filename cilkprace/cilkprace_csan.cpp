#include "cilkprace.h"
#include "stack.h"

extern std::unique_ptr<CilkpraceImpl_t> tool;
extern bool HAS_INIT;

inline unsigned worker_number() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  return __cilkrts_get_worker_number();
#pragma clang diagnostic pop
}

// Stack structures for keeping track of MAAPs for pointer arguments to function calls
Stack_t<std::pair<csi_id_t, MAAP_t>> MAAPs;
Stack_t<unsigned> MAAP_counts; 
/*static*/ inline bool checkMAAP(MAAP_t val, MAAP_t flag) {
  return static_cast<uint8_t>(val) & static_cast<uint8_t>(flag);
}

CILKSAN_API
void __csan_init() {};
CILKSAN_API
void __csan_unit_init(const char * const file_name,
                      const instrumentation_counts_t counts) {};

CILKSAN_API
void __csan_before_call(const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count, const func_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_call(fid=" << func_id << ", nsr="
      << prop.num_sync_reg << ", " << prop.may_spawn << ")" << std::endl;
#endif
}

CILKSAN_API
void __csan_after_call(const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count, const func_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] after_call(feid=" << func_id
      << ", fid=" << func_id << ", " << prop.may_spawn << ")" << std::endl;
#endif
}

 CILKSAN_API void __csan_func_entry(const csi_id_t func_id, __attribute__((noescape)) const void *bp, 
                                  __attribute__((noescape)) const void *sp, const func_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] func_entry(fid=" << func_id << ", " << prop.may_spawn << ")" << std::endl;
#endif
  if (!HAS_INIT) return;
  auto entry = (source_loc_t*) __csan_get_func_source_loc(func_id);
  outs_red << "FUNC: " << entry->name << " has " << prop.num_sync_reg << " sync regions " << std::endl;
  tool->enter_func(func_id, prop.may_spawn);

}

CILKSAN_API void __csan_func_exit(const csi_id_t func_exit_id, const csi_id_t func_id, const func_exit_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] func_exit(feid=" << func_exit_id
      << ", fid=" << func_id << ", " << prop.may_spawn << ")" << std::endl;
#endif
  tool->exit_func(func_id, prop.may_spawn);
}


CILKSAN_API void __csan_load(const csi_id_t load_id, const void *addr,
                           int32_t num_bytes, const load_prop_t prop) {
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
  // Putting this guard here shouldn't affect correctness but might make us faster
  // As we filter out reads that are about to be writes anyway
  //if (prop.is_read_before_write_in_bb)
  //  return;
  if (!HAS_INIT) return;
  auto store = (source_loc_t*) __csan_get_load_source_loc(load_id);
  tool->register_read((uint64_t)addr, num_bytes, *store);
#ifdef TRACE_CALLS
  outs_red << "LOAD ON (" << store->name << ", " << store->line_number << ")" << std::endl;
#endif
}

CILKSAN_API void __csan_large_load(const csi_id_t load_id, const void *addr,
                           int32_t num_bytes, const load_prop_t prop) {
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
  // Putting this guard here shouldn't affect correctness but might make us faster
  // As we filter out reads that are about to be writes anyway
  //if (prop.is_read_before_write_in_bb)
  //  return;
  if (!HAS_INIT) return;
  auto store = (source_loc_t*) __csan_get_load_source_loc(load_id);
  tool->register_read((uint64_t)addr, num_bytes, *store);
#ifdef TRACE_CALLS
  outs_red << "LOAD ON (" << store->name << ", " << store->line_number << ")" << std::endl;
#endif
}

CILKSAN_API void __csan_store(const csi_id_t store_id, const void *addr,
                             int32_t num_bytes, const store_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_store(sid=" << store_id
      << ", addr=" << addr << ", nb=" << num_bytes << ", align="
      << prop.alignment << ", vtab=" << prop.is_vtable_access << ", const="
      << prop.is_constant << ", stack=" << prop.is_on_stack << ", cap="
      << prop.may_be_captured << ", atomic=" << prop.is_atomic
      << ", threadlocal=" << prop.is_thread_local << ")" << std::endl;
#endif
  //TODO: Reads and writes aren't fixed-width and on the same boundaries. It's an overlapping problem. We'll have to resolve this.
  if (!HAS_INIT) return;
  auto store = (source_loc_t*) __csan_get_store_source_loc(store_id);
  tool->register_write((uint64_t)addr, num_bytes, *store);
#ifdef TRACE_CALLS
  outs_red << "WRITE ON (" << store->name << ", " << store->line_number << ")" << std::endl;
#endif
}

CILKSAN_API void __csan_large_store(const csi_id_t store_id, const void *addr,
                             int32_t num_bytes, const store_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_store(sid=" << store_id
      << ", addr=" << addr << ", nb=" << num_bytes << ", align="
      << prop.alignment << ", vtab=" << prop.is_vtable_access << ", const="
      << prop.is_constant << ", stack=" << prop.is_on_stack << ", cap="
      << prop.may_be_captured << ", atomic=" << prop.is_atomic
      << ", threadlocal=" << prop.is_thread_local << ")" << std::endl;
#endif
  //TODO: Reads and writes aren't fixed-width and on the same boundaries. It's an overlapping problem. We'll have to resolve this.
  if (!HAS_INIT) return;
  auto store = (source_loc_t*) __csan_get_store_source_loc(store_id);
  tool->register_write((uint64_t)addr, num_bytes, *store);
#ifdef TRACE_CALLS
  outs_red << "WRITE ON (" << store->name << ", " << store->line_number << ")" << std::endl;
#endif
}

CILKSAN_API void __csan_task(const csi_id_t task_id, const csi_id_t detach_id,
                             const task_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] task(tid=" << task_id << ", did="
      << detach_id << ", nsr=" << prop.num_sync_reg << ")" << std::endl;
#endif
  tool->task(task_id);
}

CILKSAN_API
void __csan_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
                     const csi_id_t detach_id, const unsigned sync_reg,
                     const task_exit_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] task_exit(teid=" << task_exit_id
      << ", tid=" << task_id << ", did=" << detach_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
  tool->exit_task(task_id);
}

CILKSAN_API
void __csan_detach(const csi_id_t detach_id, const unsigned sync_reg,
                  const detach_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] detach(did=" << detach_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
}

CILKSAN_API
void __csan_detach_continue(const csi_id_t detach_continue_id,
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

CILKSAN_API
void __csan_before_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_sync(sid=" << sync_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
}

CILKSAN_API
void __csan_after_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] after_sync(sid=" << sync_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
  tool->enter_serial();
}

CILKSAN_API
void __csan_after_alloca(const csi_id_t alloca_id, const void *addr,
                             size_t num_bytes, const alloca_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] after_alloca(aid=" << alloca_id
      << ", addr=" << addr << ", nb=" << num_bytes << ", static="
      << prop.is_static << ")" << std::endl;
#endif
  tool->register_alloca(addr, num_bytes);
}

CILKSAN_API
void __csan_before_allocfn(const csi_id_t allocfn_id, size_t size,
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

CILKSAN_API
void __csan_after_allocfn(const csi_id_t allocfn_id, const void *addr,
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

CILKSAN_API
void __csan_before_free(const csi_id_t free_id, const void *ptr,
                            const free_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_free(fid=" << free_id
      << ", addr=" << ptr << ", type=" << prop.free_ty << ")" << std::endl;
#endif
}

CILKSAN_API
void __csan_after_free(const csi_id_t free_id, const void *ptr,
                           const free_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] after_free(fid=" << free_id
      << ", addr=" << ptr << ", type=" << prop.free_ty << ")" << std::endl;
#endif
}

CILKSAN_API void __csan_set_MAAP(MAAP_t val, csi_id_t id) {
  if (!should_check())  
    return; 
#ifdef TRACE_CALLS
  outs_red << "SET MAAP " << (int)val << ", " << id << std::endl;
#endif
  MAAPs.push_back(std::make_pair(id, val));
}

CILKSAN_API void __csan_get_MAAP(MAAP_t *ptr, csi_id_t id, unsigned idx) {
#ifdef TRACE_CALLS
  outs_red << "GET MAAP " << ptr << ", " << id << ", " << idx << std::endl;
#endif
  // We presume that __csan_get_MAAP runs early in the function, so if
  // instrumentation is disabled, it's disabled for the whole function.
  if (!should_check()) {
    *ptr = MAAP_t::NoAccess;
    return;
  }

  unsigned MAAP_count = MAAP_counts.back();
  if (idx >= MAAP_count) {
    //outs_red << "No MAAP found: idx " << idx << " >= count " << MAAP_count << std::endl;
    // The stack doesn't have MAAPs for us, so assume the worst: modref with
    // aliasing.
    *ptr = MAAP_t::ModRef;
    return;
  }

  std::pair<csi_id_t, MAAP_t> MAAP = *MAAPs.ancestor(idx);
  if (MAAP.first == id) {
    //outs_red << "MAAP found: " << MAAP.second << std::endl;
    *ptr = MAAP.second;
  } else {
    //outs_red << "NO MAAP found! " << std::endl;
    // The stack doesn't have MAAPs for us, so assume the worst.
    *ptr = MAAP_t::ModRef;
  }
  *ptr = MAAP_t::NoAccess;
}

// This is what libhooks translates things in to
// Helper function for checking a function that reads len bytes starting at ptr.
void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal,
                                    uintptr_t ptr, size_t len) {
  if (!HAS_INIT) return;
  auto store = (source_loc_t*) __csan_get_load_source_loc(call_id);
#ifdef TRACE_CALLS
  outs_red << "CHECK READ ON (" << store->name << ", " << store->line_number << ")" << std::endl;
#endif
  if (checkMAAP(MAAPVal, MAAP_t::Mod)) {
    tool->register_read((uint64_t)ptr, len, *store);
  }
}
void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal,
                                    const void *ptr, size_t len) {
    check_read_bytes(call_id, MAAPVal, (uintptr_t) ptr, len);
}

// Helper function for checking a function that writes len bytes starting at
// ptr.
void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal,
                                     uintptr_t ptr, size_t len) {
  if (!HAS_INIT) return;
  auto store = (source_loc_t*) __csan_get_store_source_loc(call_id);
#ifdef TRACE_CALLS
  outs_red << "CHECK WRITE ON (" << store->name << ", " << store->line_number << ")" << std::endl;
#endif
  if (checkMAAP(MAAPVal, MAAP_t::Ref)) {
    tool->register_write((uint64_t)ptr, len, *store);
  }
}

void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal,
                                     const void *ptr, size_t len) {
    check_write_bytes(call_id, MAAPVal, (uintptr_t) ptr, len);
}


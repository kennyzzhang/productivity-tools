#include "cilkprace.h"

extern bool HAS_INIT;

inline unsigned worker_number() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  return __cilkrts_get_worker_number();
#pragma clang diagnostic pop
}

CILKSAN_API
void __csan_init() {};
CILKSAN_API
void __csan_unit_init(const char * const file_name,
                      const instrumentation_counts_t counts) {};

// Cilksan keeps MAAPs (the compiler's may-access-alias-in-parallel flags)
// across calls here. Cilkprace has no MAAP support: the driver compiles it
// with -cilksan-maap-checks=false, so MAAP_count is always 0.
CILKSAN_API
void __csan_before_call(const csi_id_t call_id, const csi_id_t func_id,
                        unsigned MAAP_count, const func_prop_t prop) {}

CILKSAN_API
void __csan_after_call(const csi_id_t call_id, const csi_id_t func_id,
                       unsigned MAAP_count, const func_prop_t prop) {}

 CILKSAN_API void __csan_func_entry(const csi_id_t func_id, __attribute__((noescape)) const void *bp, 
                                  __attribute__((noescape)) const void *sp, const func_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] func_entry(fid=" << func_id << ", " << prop.may_spawn << ")" << std::endl;
#endif
  if (!HAS_INIT) return;
  tool_instance.register_frame((uintptr_t)bp, (uintptr_t)sp);
}

CILKSAN_API void __csan_func_exit(const csi_id_t func_exit_id, const csi_id_t func_id, const func_exit_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] func_exit(feid=" << func_exit_id
      << ", fid=" << func_id << ", " << prop.may_spawn << ")" << std::endl;
#endif
}


// always_inline: top of the read path; see shadow_label::does_read_race.
__attribute__((always_inline))
CILKSAN_API void __csan_load(const csi_id_t load_id, const void *addr,
                           int32_t num_bytes, const load_prop_t prop,
                           const os_label *cur_lab) {
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
  if (CILKPRACE_UNLIKELY(!CHECKING || !cur_lab)) return;
  tool_instance.register_read((uint64_t)addr, num_bytes, load_id, *cur_lab);
}

CILKSAN_API void __csan_before_loop(const csi_id_t loop_id,
                                    const int64_t line_no,
                                    const csi_id_t *stripmined_loop_id) {
}

CILKSAN_API void __csan_after_loop(const csi_id_t loop_id,
                                   const int64_t line_no,
                                   const csi_id_t *stripmined_loop_id) {
}

CILKSAN_API void __csan_destroy_loop(const csi_id_t loop_id) {
}

// always_inline: top of the read path; see shadow_label::does_read_race.
__attribute__((always_inline))
CILKSAN_API void __csan_large_load(const csi_id_t load_id, const void *addr,
                           size_t num_bytes, const load_prop_t prop,
                           const os_label *cur_lab) {
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
  if (CILKPRACE_UNLIKELY(!CHECKING || !cur_lab)) return;
  tool_instance.register_read((uint64_t)addr, num_bytes, load_id, *cur_lab);
}

// always_inline: top of the write path; see shadow_label::does_read_race.
__attribute__((always_inline))
CILKSAN_API void __csan_store(const csi_id_t store_id, const void *addr,
                             int32_t num_bytes, const store_prop_t prop,
                             const os_label *cur_lab) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_store(sid=" << store_id
      << ", addr=" << addr << ", nb=" << num_bytes << ", align="
      << prop.alignment << ", vtab=" << prop.is_vtable_access << ", const="
      << prop.is_constant << ", stack=" << prop.is_on_stack << ", cap="
      << prop.may_be_captured << ", atomic=" << prop.is_atomic
      << ", threadlocal=" << prop.is_thread_local << ")" << std::endl;
#endif
  if (CILKPRACE_UNLIKELY(!CHECKING || !cur_lab)) return;
  tool_instance.register_write((uint64_t)addr, num_bytes, store_id, *cur_lab);
}

// always_inline: top of the write path; see shadow_label::does_read_race.
__attribute__((always_inline))
CILKSAN_API void __csan_large_store(const csi_id_t store_id, const void *addr,
                             size_t num_bytes, const store_prop_t prop,
                             const os_label *cur_lab) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_store(sid=" << store_id
      << ", addr=" << addr << ", nb=" << num_bytes << ", align="
      << prop.alignment << ", vtab=" << prop.is_vtable_access << ", const="
      << prop.is_constant << ", stack=" << prop.is_on_stack << ", cap="
      << prop.may_be_captured << ", atomic=" << prop.is_atomic
      << ", threadlocal=" << prop.is_thread_local << ")" << std::endl;
#endif
  if (CILKPRACE_UNLIKELY(!CHECKING || !cur_lab)) return;
  tool_instance.register_write((uint64_t)addr, num_bytes, store_id, *cur_lab);
}

CILKSAN_API void __csan_task(const csi_id_t task_id, const csi_id_t detach_id,
                             __attribute__((noescape)) const void *bp,
                             __attribute__((noescape)) const void *sp,
                             const task_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] task(tid=" << task_id << ", did="
      << detach_id << ", nsr=" << prop.num_sync_reg << ")" << std::endl;
#endif
  if (!HAS_INIT) return;
  tool_instance.register_frame((uintptr_t)bp, (uintptr_t)sp);
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
}

CILKSAN_API
void __csan_before_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#ifdef TRACE_CALLS
  __cilkrts_os_label lbl = __cilkrts_get_os_label();
  outs_red
      << "[W" << worker_number() << "] before_sync(sid=" << sync_id << ", sr="
      << sync_reg << ") label: " << *lbl.label << std::endl;
#endif
}

CILKSAN_API
void __csan_after_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#ifdef TRACE_CALLS
  __cilkrts_os_label lbl = __cilkrts_get_os_label();
  outs_red
      << "[W" << worker_number() << "] after_sync(sid=" << sync_id << ", sr="
      << sync_reg << ") label: " << *lbl.label << std::endl;
#endif
  
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
  tool_instance.register_alloca((uintptr_t) addr, num_bytes);
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
  if (HAS_INIT && oldaddr)
    tool_instance.register_realloc_begin((uintptr_t)oldaddr, allocfn_id);
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
  if (!HAS_INIT) return;
  if (oldaddr)
    tool_instance.register_realloc_end((uintptr_t)addr, size * num,
                                       (uintptr_t)oldaddr);
  else
    tool_instance.register_allocfn((uintptr_t)addr, size * num);
}

CILKSAN_API
void __csan_alloc_strdup(const csi_id_t allocfn_id, const csi_id_t func_id,
                         unsigned MAAP_count, const allocfn_prop_t prop,
                         char *result, const char *str) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] alloc_strdup(afid=" << allocfn_id
           << ", fid=" << func_id << ", res=" << (void *)result << ")"
           << std::endl;
#endif
  tool_instance.register_alloc_strdup((uintptr_t) result, str);
}

// Allocation functions that return their block another way; as in Cilksan,
// clear the block.
CILKSAN_API
void __csan_alloc_strndup(const csi_id_t allocfn_id, const csi_id_t func_id,
                          unsigned MAAP_count, const allocfn_prop_t prop,
                          char *result, const char *str, size_t size) {
  if (HAS_INIT && result)
    tool_instance.register_allocfn((uintptr_t)result, strlen(result) + 1);
}

CILKSAN_API
void __csan_alloc_posix_memalign(const csi_id_t allocfn_id,
                                 const csi_id_t func_id, unsigned MAAP_count,
                                 const allocfn_prop_t prop, int result,
                                 void **ptr, size_t alignment, size_t size) {
  if (HAS_INIT && result == 0)
    tool_instance.register_allocfn((uintptr_t)*ptr, size);
}

CILKSAN_API
void __csan_alloc_memalign(const csi_id_t allocfn_id, const csi_id_t func_id,
                           unsigned MAAP_count, const allocfn_prop_t prop,
                           char *result, size_t alignment, size_t size) {
  if (HAS_INIT)
    tool_instance.register_allocfn((uintptr_t)result, size);
}

CILKSAN_API
void __csan_before_free(const csi_id_t free_id, const void *ptr,
                            const free_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red
      << "[W" << worker_number() << "] before_free(fid=" << free_id
      << ", addr=" << ptr << ", type=" << prop.free_ty << ")" << std::endl;
#endif
  // Before the free: once it returns, a parallel malloc can get the block.
  if (HAS_INIT)
    tool_instance.register_heap_free((uintptr_t)ptr, free_id, free_site_t::free);
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

// This is what libhooks translates things in to
void check_read_bytes(csi_id_t call_id, uintptr_t ptr, size_t len) {
  if (!CHECKING) return;
#ifdef TRACE_CALLS
  auto store = (const source_loc_t*) __csan_get_load_source_loc(call_id);

  outs_red << "CHECK READ ON (" << (store && store->name ? store->name : "null") << ", " << (store ? store->line_number : 0) << ")" << std::endl;
#endif
  tool_instance.register_read((uint64_t)ptr, len, call_id);
}
void check_read_bytes(csi_id_t call_id, const void *ptr, size_t len) {
    check_read_bytes(call_id, (uintptr_t) ptr, len);
}

// Helper function for checking a function that writes len bytes starting at
// ptr.
void check_write_bytes(csi_id_t call_id, uintptr_t ptr, size_t len) {
  if (!CHECKING) return;
#ifdef TRACE_CALLS
  auto store = (const source_loc_t*) __csan_get_store_source_loc(call_id);
  outs_red << "CHECK WRITE ON (" << (store && store->name ? store->name : "null") << ", " << (store ? store->line_number : 0) << ")" << std::endl;
#endif
  tool_instance.register_write((uint64_t)ptr, len, call_id);
}

void check_write_bytes(csi_id_t call_id, const void *ptr, size_t len) {
    check_write_bytes(call_id, (uintptr_t) ptr, len);
}


// outside world (including runtime).
// Non-inlined version for user code to use
// Checking can be turned off around code that should not be race-checked
// (e.g. benchmark setup). Calls nest. Only call these from serial code: they
// update plain globals that the access hooks read. Allocation hooks keep
// running while checking is off, so reused memory is still cleared.
CILKSAN_API void __cilksan_enable_checking(void) {
  CHECKING_DISABLED--;
  CHECKING = HAS_INIT && CHECKING_DISABLED == 0;
}

// Non-inlined version for user code to use
CILKSAN_API void __cilksan_disable_checking(void) {
  CHECKING_DISABLED++;
  CHECKING = false;
}


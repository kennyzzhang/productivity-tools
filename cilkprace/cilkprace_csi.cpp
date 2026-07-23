#include "cilkprace.h"

extern std::unique_ptr<CilkpraceImpl_t> tool;
extern bool HAS_INIT;

inline unsigned worker_number() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  return __cilkrts_get_worker_number();
#pragma clang diagnostic pop
}

/*static*/ inline bool checkMAAP(MAAP_t val, MAAP_t flag) {
  return static_cast<uint8_t>(val) & static_cast<uint8_t>(flag);
}

CILKTOOL_API void __csi_init() {}

CILKTOOL_API void __csi_unit_init(const char *const file_name,
                                  const instrumentation_counts_t counts) {
  HAS_INIT = true;
}

CILKTOOL_API
void __csi_func_entry(const csi_id_t func_id, const func_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] func(fid=" << func_id
           << ", nsr=" << prop.num_sync_reg << ", " << prop.may_spawn << ")"
           << std::endl;
  auto entry = __csi_get_func_source_loc(func_id);
  outs_red << "FUNC: " << entry->name << " has " << prop.num_sync_reg
           << " sync regions " << std::endl;
#endif
}

CILKTOOL_API
void __csi_func_exit(const csi_id_t func_exit_id, const csi_id_t func_id,
                     const func_exit_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] func_exit(feid=" << func_exit_id
           << ", fid=" << func_id << ", " << prop.may_spawn << ")" << std::endl;
#endif
}

CILKTOOL_API void __csi_before_load(const csi_id_t load_id, const void *addr,
                                    const int32_t num_bytes,
                                    const load_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] before_load(lid=" << load_id
           << ", addr=" << addr << ", nb=" << num_bytes
           << ", align=" << prop.alignment << ", vtab=" << prop.is_vtable_access
           << ", const=" << prop.is_constant << ", stack=" << prop.is_on_stack
           << ", cap=" << prop.may_be_captured << ", atomic=" << prop.is_atomic
           << ", threadlocal=" << prop.is_thread_local
           << ", basic_read_before_write=" << prop.is_read_before_write_in_bb
           << ")" << std::endl;
#endif
  // Putting this guard here shouldn't affect correctness but might make us
  // faster As we filter out reads that are about to be writes anyway
  // if (prop.is_read_before_write_in_bb)
  //  return;
  auto store = __csi_get_load_source_loc(load_id);
  tool->register_read((uint64_t)addr, num_bytes, store);
}

CILKTOOL_API void __csi_after_load(const csi_id_t load_id, const void *addr,
                                   const int32_t num_bytes,
                                   const load_prop_t prop) {
  return;
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] after_load(lid=" << load_id
           << ", addr=" << addr << ", nb=" << num_bytes
           << ", align=" << prop.alignment << ", vtab=" << prop.is_vtable_access
           << ", const=" << prop.is_constant << ", stack=" << prop.is_on_stack
           << ", cap=" << prop.may_be_captured << ", atomic=" << prop.is_atomic
           << ", threadlocal=" << prop.is_thread_local
           << ", basic_read_before_write=" << prop.is_read_before_write_in_bb
           << ")" << std::endl;
#endif
}

CILKTOOL_API void __csi_before_store(const csi_id_t store_id, const void *addr,
                                     const int32_t num_bytes,
                                     const store_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] before_store(sid=" << store_id
           << ", addr=" << addr << ", nb=" << num_bytes
           << ", align=" << prop.alignment << ", vtab=" << prop.is_vtable_access
           << ", const=" << prop.is_constant << ", stack=" << prop.is_on_stack
           << ", cap=" << prop.may_be_captured << ", atomic=" << prop.is_atomic
           << ", threadlocal=" << prop.is_thread_local << ")" << std::endl;
#endif
  // TODO: Reads and writes aren't fixed-width and on the same boundaries. It's
  // an overlapping problem. We'll have to resolve this.
  auto store = __csi_get_store_source_loc(store_id);
  tool->register_write((uint64_t)addr, num_bytes, store);
}

CILKTOOL_API void __csi_after_store(const csi_id_t store_id, const void *addr,
                                    const int32_t num_bytes,
                                    const store_prop_t prop) {
  return;
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] after_store(sid=" << store_id
           << ", addr=" << addr << ", nb=" << num_bytes
           << ", align=" << prop.alignment << ", vtab=" << prop.is_vtable_access
           << ", const=" << prop.is_constant << ", stack=" << prop.is_on_stack
           << ", cap=" << prop.may_be_captured << ", atomic=" << prop.is_atomic
           << ", threadlocal=" << prop.is_thread_local << ")" << std::endl;
#endif
}

CILKTOOL_API void __csi_task(const csi_id_t task_id, const csi_id_t detach_id,
                             const task_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] task(tid=" << task_id
           << ", did=" << detach_id << ", nsr=" << prop.num_sync_reg << ")"
           << std::endl;
#endif
}

CILKTOOL_API
void __csi_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
                     const csi_id_t detach_id, const unsigned sync_reg,
                     const task_exit_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] task_exit(teid=" << task_exit_id
           << ", tid=" << task_id << ", did=" << detach_id
           << ", sr=" << sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_detach(const csi_id_t detach_id, const unsigned sync_reg,
                  const detach_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] detach(did=" << detach_id
           << ", sr=" << sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_detach_continue(const csi_id_t detach_continue_id,
                           const csi_id_t detach_id, const unsigned sync_reg,
                           const detach_continue_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number()
           << "] detach_continue(dcid=" << detach_continue_id
           << ", did=" << detach_id << ", sr=" << sync_reg
           << ", unwind=" << prop.is_unwind << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_before_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] before_sync(sid=" << sync_id
           << ", sr=" << sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_after_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] after_sync(sid=" << sync_id
           << ", sr=" << sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_after_alloca(const csi_id_t alloca_id, const void *addr,
                        size_t num_bytes, const alloca_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] after_alloca(aid=" << alloca_id
           << ", addr=" << addr << ", nb=" << num_bytes
           << ", static=" << prop.is_static << ")" << std::endl;
#endif
  tool->register_alloca(addr, num_bytes);
}

CILKTOOL_API
void __csi_before_allocfn(const csi_id_t allocfn_id, size_t size, size_t num,
                          size_t alignment, const void *oldaddr,
                          const allocfn_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] before_allocfn(afid=" << allocfn_id
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
  outs_red << "[W" << worker_number() << "] after_allocfn(afid=" << allocfn_id
           << ", addr=" << addr << ", size=" << size << ", num=" << num
           << ", align=" << alignment << ", oaddr=" << oldaddr
           << ", type=" << prop.allocfn_ty << ")" << std::endl;
#endif

  tool->register_allocfn(addr, num);
}

CILKTOOL_API
void __csi_before_free(const csi_id_t free_id, const void *ptr,
                       const free_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] before_free(fid=" << free_id
           << ", addr=" << ptr << ", type=" << prop.free_ty << ")" << std::endl;
#endif

  tool->register_free(ptr);
}

CILKTOOL_API
void __csi_after_free(const csi_id_t free_id, const void *ptr,
                      const free_prop_t prop) {
#ifdef TRACE_CALLS
  outs_red << "[W" << worker_number() << "] after_free(fid=" << free_id
           << ", addr=" << ptr << ", type=" << prop.free_ty << ")" << std::endl;
#endif
}

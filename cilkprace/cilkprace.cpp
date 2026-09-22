#include "cilkprace.h"
#include <mutex>

#pragma GCC visibility push(default)

CilkpraceImpl_t::CilkpraceImpl_t() {
#ifdef TRACE_CALLS
  fprintf(stderr, "HAS INIT\n");
#endif
  const char *env_val = getenv("CILKPRACE_IGNORE_STDLIB_RACES");
  if (env_val && strcmp(env_val, "0") == 0) {
    ignore_stdlib_races = false;
  } else {
    ignore_stdlib_races = true;
  }
  HAS_INIT = true;
}

CilkpraceImpl_t::~CilkpraceImpl_t() {}

bool CilkpraceImpl_t::is_benign_stdlib_race(uintptr_t race_addr) {
  if (!ignore_stdlib_races) return false;
  Dl_info info;
  if (dladdr((void*)race_addr, &info) && info.dli_sname) {
    if (strstr(info.dli_sname, "cout") != nullptr ||
        strstr(info.dli_sname, "cerr") != nullptr) {
      return true;
    }
  }
  return false;
}

CilkpraceImpl_t tool_instance;

void CilkpraceImpl_t::report_write_race(uintptr_t addr, csi_id_t store_id,
                                        const os_label& cur_lab, const shadow_label& lab) {
  if (ignore_stdlib_races && is_benign_stdlib_race(addr)) return;
  auto store = __csan_get_store_source_loc(store_id);
  fprintf(stderr, "WRITE RACE ON BYTE %lx (+%zu), return_addr=%p\n",
          (unsigned long)addr, shadow_mem.vmem_shadow_granularity,
          __builtin_return_address(0));
  if (store)
    fprintf(stderr, "@ %s Ln %d Col %d\n",
            store->filename ? store->filename : "<unknown>",
            store->line_number, store->column_number);
  fprintf(stderr, "======================\n");
  _exit(EXIT_FAILURE);
}

void CilkpraceImpl_t::report_read_race(uintptr_t addr, csi_id_t load_id,
                                       const os_label& cur_lab, const shadow_label& lab) {
  if (ignore_stdlib_races && is_benign_stdlib_race(addr)) return;
  auto store = __csi_get_load_source_loc(load_id);
  fprintf(stderr, "READ RACE ON BYTE %lx (+%zu)\n", (unsigned long)addr, shadow_mem.vmem_shadow_granularity);
  if (store)
    fprintf(stderr, "@ %s Ln %d Col %d\n",
            store->filename ? store->filename : "<unknown>",
            store->line_number, store->column_number);
  fprintf(stderr, "======================\n");
  _exit(EXIT_FAILURE);
}

void CilkpraceImpl_t::register_write(uintptr_t beg, size_t num_bytes,
                                     csi_id_t store_id,
                                     const os_label& cur_lab) {
  if (CILKPRACE_UNLIKELY(num_bytes == 0)) return;
  size_t gran = shadow_mem.vmem_shadow_granularity;
  size_t num_granules = (num_bytes + gran - 1) / gran;
  shadow_label *labels = &shadow_mem.addr_to_shadow(beg);
#if CILKPRACE_ABL_GRANULE_UNROLL
  #pragma unroll 2
#endif
  for (size_t i = 0; i < num_granules; ++i) {
    if (CILKPRACE_UNLIKELY(labels[i].does_write_race(cur_lab))) {
      report_write_race(beg + i * gran, store_id, cur_lab, labels[i]);
    }
  }
}

void CilkpraceImpl_t::register_write(uintptr_t beg, size_t num_bytes,
                                     csi_id_t store_id) {
  register_write(beg, num_bytes, store_id, *__cilkrts_get_current_os_label());
}

void CilkpraceImpl_t::register_read(uintptr_t beg, size_t num_bytes,
                                    csi_id_t load_id,
                                    const os_label& cur_lab) {
  if (CILKPRACE_UNLIKELY(num_bytes == 0)) return;
  size_t gran = shadow_mem.vmem_shadow_granularity;
  size_t num_granules = (num_bytes + gran - 1) / gran;
  shadow_label *labels = &shadow_mem.addr_to_shadow(beg);
#if CILKPRACE_ABL_GRANULE_UNROLL
  #pragma unroll 2
#endif
  for (size_t i = 0; i < num_granules; ++i) {
    if (CILKPRACE_UNLIKELY(labels[i].does_read_race(cur_lab))) {
      report_read_race(beg + i * gran, load_id, cur_lab, labels[i]);
    }
  }
}

void CilkpraceImpl_t::register_read(uintptr_t beg, size_t num_bytes,
                                    csi_id_t load_id) {
  register_read(beg, num_bytes, load_id, *__cilkrts_get_current_os_label());
}

void CilkpraceImpl_t::register_alloca(uintptr_t beg, size_t num_bytes) {
  if (num_bytes == 0) return;
  size_t gran = shadow_mem.vmem_shadow_granularity;
  size_t start_gran = beg / gran;
  size_t end_gran = (beg + num_bytes - 1) / gran;
  size_t num_granules = end_gran - start_gran + 1;
  shadow_label *labels = &shadow_mem.addr_to_shadow(beg);
  memset(labels, 0, num_granules * sizeof(shadow_label));
}

void CilkpraceImpl_t::register_allocfn(uintptr_t addr, size_t nb) {
  register_alloca(addr, nb);
}

void CilkpraceImpl_t::register_alloc_strdup(uintptr_t addr, const char *str) {
  if (addr && str)
    register_alloca(addr, strlen(str) + 1);
}

void CilkpraceImpl_t::register_free(uintptr_t addr) {
  fprintf(stderr, "UNHANDLED FREE\n");
}

void CilkpraceImpl_t::advance_stack_frame(uintptr_t addr) {
  fprintf(stderr, "UNHANDLED STACK ADVANCE\n");
}

void CilkpraceImpl_t::restore_stack(const csi_id_t call_id, uintptr_t addr) {
  fprintf(stderr, "UNHANDLED STACK RESTORE\n");
}

void init_MAAPstack(void *view) {
#if TRACE_CALLS
  std::cerr << "init MAAPSTACK" << std::endl;
#endif
  new (view) MAAPstack();
}

void reduce_MAAPstack(void *left_view, void *right_view) {
#if TRACE_CALLS
  std::cerr << "reduce MAAPSTACK" << std::endl;
#endif
  MAAPstack *left = static_cast<MAAPstack *>(left_view);
  MAAPstack *right = static_cast<MAAPstack *>(right_view);
  
  int32_t net_change = static_cast<int32_t>(right->size()) - 1;
  if (net_change < 0) {
    for (int32_t i = 0; i < -net_change; ++i) left->pop();
  } else if (net_change > 0) {
    for (int32_t i = 0; i < net_change; ++i) {
      left->push_back(right->from_back(net_change - 1 - i));
    }
  }
  
  right->~MAAPstack();
}

void init_ustack(void *view) {
#if TRACE_CALLS
  std::cerr << "init ustack" << std::endl;
#endif
  new (view) ustack();
}

void reduce_ustack(void *left_view, void *right_view) {
#if TRACE_CALLS
  std::cerr << "reduce ustack" << std::endl;
#endif
  ustack *left = static_cast<ustack *>(left_view);
  ustack *right = static_cast<ustack *>(right_view);
  
  int32_t net_change = static_cast<int32_t>(right->size()) - 1;
  if (net_change < 0) {
    for (int32_t i = 0; i < -net_change; ++i) left->pop();
  } else if (net_change > 0) {
    for (int32_t i = 0; i < net_change; ++i) {
      left->push_back(right->from_back(net_change - 1 - i));
    }
  }
  
  right->~ustack();
}

template class Stack_t<std::pair<csi_id_t, MAAP_t>>;
template class Stack_t<unsigned>;
template class Stack_t<uint8_t>;

#pragma GCC visibility pop

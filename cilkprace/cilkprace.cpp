#include "cilkprace.h"

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

__attribute__((visibility("default"))) CilkpraceImpl_t tool_instance;

__attribute__((noinline, cold, preserve_most, visibility("default")))
void CilkpraceImpl_t::report_write_race(uintptr_t addr, csi_id_t store_id,
                                        const os_label& cur_lab, const shadow_label& lab) {
  if (ignore_stdlib_races && is_benign_stdlib_race(addr)) return;
  auto store = __csi_get_store_source_loc(store_id);
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

__attribute__((noinline, cold, preserve_most, visibility("default")))
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

__attribute__((noinline, cold, preserve_most, visibility("default")))
bool shadow_label::does_read_race_slow(const os_label &reader) {
  range_check read_race;
  range_check write_race;

  seqlock.begin_write();

  if (__builtin_expect(last_writer.is_unraceable() && last_reader_range.is_unraceable(), 0)) {
    last_reader_range.copy_from(reader);
    is_range = false;
    seqlock.end_write();
    return false;
  }

  if (!is_range && reader.is_identical(last_reader_range)) {
    read_race = identical;
  } else {
    read_race = last_reader_range.is_unraceable()
                    ? synced
                    : reader.range_relation(last_reader_range, is_range);
  }

  if (read_race == within || read_race == identical) {
    seqlock.end_write();
    return false;
  }

  if (last_writer.is_unraceable() || reader.is_identical(last_writer)) {
    write_race = synced;
  } else {
    write_race = reader.range_relation(last_writer, false);
  }

  switch (read_race) {
  case synced:
    last_reader_range.copy_from(reader);
    is_range = false;
    break;
  case parallel:
    is_range = true;
    reader.expand_parallel_range(last_reader_range);
    break;
  default:
    break;
  }

  // Do not clear last_writer here to prevent read/write fastpath ping-pong.
  seqlock.end_write();

  return write_race == parallel || write_race == within;
}

__attribute__((noinline, cold, preserve_most, visibility("default")))
bool shadow_label::does_write_race_slow(const os_label &writer) {
  range_check read_race;
  range_check write_race;

  seqlock.begin_write();

  if (__builtin_expect(last_writer.is_unraceable() && last_reader_range.is_unraceable(), 0)) {
    last_writer.copy_from(writer);
    last_reader_range.copy_from(writer);
    is_range = false;
    seqlock.end_write();
    return false;
  }

  read_race = last_reader_range.is_unraceable()
                  ? synced
                  : writer.range_relation(last_reader_range, is_range);
  write_race = last_writer.is_unraceable()
                   ? synced
                   : writer.range_relation(last_writer, false);
  if (write_race != identical) {
    last_writer.copy_from(writer);
  }
  if (read_race == synced) {
    last_reader_range.copy_from(writer);
    is_range = false;
  }
  seqlock.end_write();

  return (read_race == parallel || read_race == within) ||
         (write_race == parallel || write_race == within);
}

bool shadow_label::does_read_race(const os_label &reader) {
  uint32_t seq;
  bool is_same_reader = false;

  do {
    seq = seqlock.begin_read();
    if (__builtin_expect(!is_range, 1)) {
      is_same_reader = reader.is_identical(last_reader_range);
    } else {
      range_check rel = reader.range_relation(last_reader_range, true);
      is_same_reader = (rel == within || rel == identical);
    }
  } while (!seqlock.read_was_safe(seq));

  if (__builtin_expect(is_same_reader, 1)) {
    return false;
  }

  return does_read_race_slow(reader);
}

bool shadow_label::does_write_race(const os_label &writer) {
  uint32_t seq;
  bool is_same_writer = false;

  do {
    seq = seqlock.begin_read();
    is_same_writer = writer.is_identical(last_writer);
  } while (!seqlock.read_was_safe(seq));

  if (__builtin_expect(is_same_writer, 1)) {
    return false;
  }

  return does_write_race_slow(writer);
}

void CilkpraceImpl_t::register_write(uintptr_t beg, size_t num_bytes,
                                     csi_id_t store_id,
                                     const os_label& cur_lab) {
  if (__builtin_expect(num_bytes == 0, 0)) return;
  size_t gran = shadow_mem.vmem_shadow_granularity;
  size_t num_granules = (num_bytes + gran - 1) / gran;
  shadow_label *labels = &shadow_mem.addr_to_shadow(beg);
  #pragma unroll 2
  for (size_t i = 0; i < num_granules; ++i) {
    if (__builtin_expect(labels[i].does_write_race(cur_lab), 0)) {
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
  if (__builtin_expect(num_bytes == 0, 0)) return;
  size_t gran = shadow_mem.vmem_shadow_granularity;
  size_t num_granules = (num_bytes + gran - 1) / gran;
  shadow_label *labels = &shadow_mem.addr_to_shadow(beg);
  #pragma unroll 2
  for (size_t i = 0; i < num_granules; ++i) {
    if (__builtin_expect(labels[i].does_read_race(cur_lab), 0)) {
      report_read_race(beg + i * gran, load_id, cur_lab, labels[i]);
    }
  }
}

void CilkpraceImpl_t::register_read(uintptr_t beg, size_t num_bytes,
                                    csi_id_t load_id) {
  register_read(beg, num_bytes, load_id, *__cilkrts_get_current_os_label());
}

void CilkpraceImpl_t::register_alloca(uintptr_t beg, size_t num_bytes) {
  if (__builtin_expect(num_bytes == 0, 0)) return;
  size_t gran = shadow_mem.vmem_shadow_granularity;
  size_t num_granules = (num_bytes + gran - 1) / gran;
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

template class __attribute__((visibility("default"))) Stack_t<std::pair<csi_id_t, MAAP_t>>;
template class __attribute__((visibility("default"))) Stack_t<unsigned>;
template class __attribute__((visibility("default"))) Stack_t<uint8_t>;





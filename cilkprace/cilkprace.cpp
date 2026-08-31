#include "cilkprace.h"

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

// Force external symbols for inline member functions so they are always exported by the runtime dylib
__attribute__((visibility("default")))
void __cilkprace_export_anchor() {
  volatile auto p1 = &os_label::range_relation;
  volatile auto p2 = &os_label::is_identical_slow;
  (void)p1;
  (void)p2;
}



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

__attribute__((visibility("default")))
bool shadow_label::does_read_race(const os_label &reader) {
    unsigned lca_depth = active_reader.lca(reader);
    if (lca_depth < write_depth) {
        write_depth = lca_depth;
    }
    if (write_depth % 4 == 3) {
        return true;
    }
    if (lca_depth % 4 != 3) {
        active_reader = reader;
    } else {
        // Technically unnecessary for one-worker execution, but could help
        // prune later lca calls
        active_reader.end_idx = lca_depth;
    }
    return false;
}

__attribute__((visibility("default")))
bool shadow_label::does_write_race(const os_label &writer) {
    unsigned lca_depth = active_reader.lca(writer);
    if (lca_depth < write_depth) {
        write_depth = lca_depth;
    }
    if (write_depth % 4 == 3) {
        return true;
    }
    if (lca_depth % 4 != 3) {
        active_reader = writer;
        write_depth = writer.end_idx;
    } else {
        return true;
    }
    return false;
}

// Force external symbols for inline member functions so they are always exported by the runtime dylib
__attribute__((visibility("default")))
void __cilkprace_export_anchor() {
  volatile auto p1 = &shadow_label::does_read_race;
  volatile auto p2 = &shadow_label::does_write_race;
  (void)p1;
  (void)p2;
}



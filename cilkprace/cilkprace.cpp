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
#if CILKPRACE_USE_LEB8_PTR
  leb8_ptr_init();
#endif
  HAS_INIT = true;
  CHECKING = CHECKING_DISABLED == 0;
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

#if CILKPRACE_ADAPTIVE_GRANULE
// Writes of at least this many bytes merge the lines they cover back.
constexpr size_t kMergeMinBytes = 1024;

// Out of line: the accesses the inline line path doesn't handle. Returns the
// first racing address in [beg, beg + num_bytes), or 0; the caller reports it
// and, if the report returns (a race it ignores), resumes at the next byte.
template <bool Write>
__attribute__((noinline)) uintptr_t
CilkpraceImpl_t::check_part_range(uintptr_t beg, size_t num_bytes,
                                  const os_label &cur) {
  constexpr uintptr_t G = CILKPRACE_GRANULARITY;
  uintptr_t end = beg + num_bytes;
  shadow_label *s = &shadow_mem.addr_to_shadow(beg);
  for (uintptr_t g = beg / G * G; g < end; g += G, ++s) {
    unsigned lo = g < beg ? beg - g : 0, hi = end - g < G ? end - g : G;
    if (lo == 0 && hi == G) {
      // Unlikely: only on a race. A reported race exits the program; only a
      // race the tool ignores (CILKPRACE_IGNORE_STDLIB_RACES: cout, cerr)
      // returns to the caller, which then checks the rest of the range.
      if (CILKPRACE_UNLIKELY(Write ? s->does_write_race(cur) : s->does_read_race(cur)))
        return g;
    } else {
      // The line path first (crossing accesses come here from the inline
      // check with lines it could handle), then the general one.
      unsigned r = Write ? s->does_write_race_line(cur, lo, hi - lo)
                         : s->does_read_race_line(cur, lo, hi - lo);
      // Unlikely: only on a race.
      if (CILKPRACE_UNLIKELY(r >= 2))
        return g + r - 2;
      if (r == 1) {
        if (unsigned q = Write ? s->does_write_race_part(cur, lo, hi)
                               : s->does_read_race_part(cur, lo, hi))
          return g + q - 1;
      }
    }
  }
  return 0;
}
template uintptr_t CilkpraceImpl_t::check_part_range<true>(uintptr_t, size_t,
                                                           const os_label &);
template uintptr_t CilkpraceImpl_t::check_part_range<false>(uintptr_t, size_t,
                                                            const os_label &);
#endif

void CilkpraceImpl_t::register_write(uintptr_t beg, size_t num_bytes,
                                     csi_id_t store_id,
                                     const os_label& cur_lab) {
  if (CILKPRACE_UNLIKELY(num_bytes == 0)) return;
  size_t gran = shadow_mem.vmem_shadow_granularity;
  size_t num_granules = (num_bytes + gran - 1) / gran;
  shadow_label *labels = &shadow_mem.addr_to_shadow(beg);
#if CILKPRACE_ADAPTIVE_GRANULE
  // No hint: with lines longer than an access, most accesses cover part of one.
  if ((beg | num_bytes) % gran) {
    uintptr_t a = beg;
    unsigned r = labels->does_write_race_line(cur_lab, beg % gran, num_bytes);
    if (r == 0)
      return;
    if (r >= 2) {
      a = beg / gran * gran + r - 2;
      report_write_race(a, store_id, cur_lab, *labels);
      ++a;
    }
    for (uintptr_t end = beg + num_bytes; a < end; ++a) {
      a = check_part_range<true>(a, end - a, cur_lab);
      if (!a)
        break;
      report_write_race(a, store_id, cur_lab, *labels);
    }
    return;
  }
#endif
#if CILKPRACE_ABL_GRANULE_UNROLL
  #pragma unroll 2
#endif
  for (size_t i = 0; i < num_granules; ++i) {
    if (CILKPRACE_UNLIKELY(labels[i].does_write_race(cur_lab))) {
      report_write_race(beg + i * gran, store_id, cur_lab, labels[i]);
    }
  }
#if CILKPRACE_ADAPTIVE_GRANULE
  // A long write (a hoisted range check, a large copy) leaves the split lines
  // it covers all its own: merge them back, so later checks of them are one
  // entry each. Short writes don't, since in scattered writes a line would
  // merge and split again.
  if (num_bytes >= kMergeMinBytes)
    for (size_t i = 0; i < num_granules; ++i)
#if CILKPRACE_USE_LEB8_FLAT
      if (CILKPRACE_UNLIKELY(leb8_flat_refined(labels[i])))
        leb8_flat_merge(labels[i]);
#else
      if (uint64_t w = labels[i].entry; CILKPRACE_UNLIKELY(leb8_short_ptr_refined(w)))
        leb8_short_ptr_merge(labels[i], w);
#endif
#endif
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
#if CILKPRACE_ADAPTIVE_GRANULE
  // No hint: with lines longer than an access, most accesses cover part of one.
  if ((beg | num_bytes) % gran) {
    uintptr_t a = beg;
    unsigned r = labels->does_read_race_line(cur_lab, beg % gran, num_bytes);
    if (r == 0)
      return;
    if (r >= 2) {
      a = beg / gran * gran + r - 2;
      report_read_race(a, load_id, cur_lab, *labels);
      ++a;
    }
    for (uintptr_t end = beg + num_bytes; a < end; ++a) {
      a = check_part_range<false>(a, end - a, cur_lab);
      if (!a)
        break;
      report_read_race(a, load_id, cur_lab, *labels);
    }
    return;
  }
#endif
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
#if CILKPRACE_ADAPTIVE_GRANULE
  // Exactly the range: a line may hold other live bytes.
  (void)num_granules;
#if CILKPRACE_USE_LEB8_FLAT
  leb8_flat_clear(labels, beg, num_bytes);
#else
  leb8_short_ptr_clear(labels, beg, num_bytes);
#endif
#else
  memset(labels, 0, num_granules * sizeof(shadow_label));
#endif
}

static size_t heap_hash(uintptr_t addr) { return (addr >> 4) * 0x9E3779B97F4A7C15ULL; }

// Doubles the table (at least 1024 slots), dropping removed slots.
heap_sizes_t::shard_t &heap_sizes_t::grow(shard_t &s) {
  size_t cap = s.cap ? 2 * s.cap : 1024;
  uintptr_t *keys = (uintptr_t *)calloc(cap, sizeof(uintptr_t));
  size_t *sizes = (size_t *)malloc(cap * sizeof(size_t));
  size_t used = 0;
  for (size_t i = 0; i < s.cap; ++i) {
    if (s.keys[i] <= 1) continue;
    size_t j = heap_hash(s.keys[i]) & (cap - 1);
    while (keys[j]) j = (j + 1) & (cap - 1);
    keys[j] = s.keys[i];
    sizes[j] = s.sizes[i];
    ++used;
  }
  free(s.keys);
  free(s.sizes);
  s.keys = keys, s.sizes = sizes, s.cap = cap, s.used = used;
  return s;
}

void heap_sizes_t::insert(uintptr_t addr, size_t size) {
  shard_t &s = shards[(addr >> 4) % NSHARDS];
  std::lock_guard<std::mutex> g(s.lock);
  if (2 * (s.used + 1) > s.cap) grow(s);
  size_t j = heap_hash(addr) & (s.cap - 1), slot = s.cap;
  for (; s.keys[j]; j = (j + 1) & (s.cap - 1)) {
    if (s.keys[j] == addr) { s.sizes[j] = size; return; }
    if (s.keys[j] == 1 && slot == s.cap) slot = j;
  }
  if (slot == s.cap) { slot = j; ++s.used; }
  s.keys[slot] = addr;
  s.sizes[slot] = size;
}

bool heap_sizes_t::remove(uintptr_t addr, size_t &size) {
  shard_t &s = shards[(addr >> 4) % NSHARDS];
  std::lock_guard<std::mutex> g(s.lock);
  if (!s.cap) return false;
  for (size_t j = heap_hash(addr) & (s.cap - 1); s.keys[j];
       j = (j + 1) & (s.cap - 1)) {
    if (s.keys[j] == addr) {
      s.keys[j] = 1;
      size = s.sizes[j];
      return true;
    }
  }
  return false;
}

void CilkpraceImpl_t::register_allocfn(uintptr_t addr, size_t nb) {
  if (!addr || nb == 0) return;
  heap_sizes.insert(addr, nb);
  register_alloca(addr, nb);
}

void CilkpraceImpl_t::register_alloc_strdup(uintptr_t addr, const char *str) {
  if (addr && str)
    register_allocfn(addr, strlen(str) + 1);
}

// Cilksan's stack semantics: a frame's shadow is cleared when the frame is
// popped. Cilksan finds the frame's range at exit on a stack of entry records,
// which works only in a serial run; after a steal here, the continuation's
// strand no longer has its entry record. So clear the same range, [sp, bp),
// when the frame is pushed instead: the addresses are still cleared before any
// instrumented frame uses them again. Dynamic allocas are cleared at their
// alloca hook, as in Cilksan. Like Cilksan, treat a frame whose bp is more
// than a stack away from sp (bp still on the old stack after a stack switch)
// as empty.
void CilkpraceImpl_t::register_frame(uintptr_t bp, uintptr_t sp) {
  static constexpr uintptr_t DEFAULT_STACK_SIZE = 1UL << 21;  // as in Cilksan
  if (bp > sp && bp - sp <= DEFAULT_STACK_SIZE)
    register_alloca(sp, bp - sp);
}

// Cilksan's heap semantics: a free (or the part of a block a realloc gives up)
// counts as a write to the whole block, so a free in parallel with an access
// to the block is a race. The next allocation of the memory clears it. With
// checking off (or no label yet), the block is only cleared (Cilksan's
// mark_free).
void CilkpraceImpl_t::register_free(uintptr_t beg, size_t num_bytes,
                                    csi_id_t free_id, free_site_t site) {
  if (num_bytes == 0) return;
  const os_label *lab = CHECKING ? __cilkrts_get_current_os_label() : nullptr;
  if (!lab) {
    register_alloca(beg, num_bytes);
    return;
  }
  const os_label &cur_lab = *lab;
  size_t gran = shadow_mem.vmem_shadow_granularity;
  size_t num_granules = (num_bytes + gran - 1) / gran;
  shadow_label *labels = &shadow_mem.addr_to_shadow(beg);
#if CILKPRACE_ADAPTIVE_GRANULE
  if ((beg | num_bytes) % gran) {
    for (uintptr_t a = beg, end = beg + num_bytes; a < end; ++a) {
      a = check_part_range<true>(a, end - a, cur_lab);
      if (!a)
        break;
      report_free_race(a, free_id, site);
    }
    return;
  }
#endif
  for (size_t i = 0; i < num_granules; ++i)
    if (CILKPRACE_UNLIKELY(labels[i].does_write_race(cur_lab)))
      report_free_race(beg + i * gran, free_id, site);
}

void CilkpraceImpl_t::register_heap_free(uintptr_t addr, csi_id_t free_id,
                                         free_site_t site) {
  size_t size;
  if (addr && heap_sizes.remove(addr, size))
    register_free(addr, size, free_id, site);
}

// A realloc, in two hooks. The old block must be checked before the call:
// once it returns, a parallel malloc can get the freed memory. The call may
// free the whole block, so before it the whole block counts as written (in
// place, Cilksan counts only the part given up). Same thread for both hooks:
// an allocation function doesn't spawn.
static thread_local size_t realloc_old_size;
static thread_local bool realloc_old_known;

void CilkpraceImpl_t::register_realloc_begin(uintptr_t oldaddr,
                                             csi_id_t allocfn_id) {
  realloc_old_known = heap_sizes.remove(oldaddr, realloc_old_size);
  if (realloc_old_known)
    register_free(oldaddr, realloc_old_size, allocfn_id, free_site_t::realloc);
}

void CilkpraceImpl_t::register_realloc_end(uintptr_t addr, size_t new_size,
                                           uintptr_t oldaddr) {
  size_t old_size = realloc_old_size;
  bool known = realloc_old_known;
  if (!addr && new_size) {
    // Failed: the old block is untouched.
    if (known) heap_sizes.insert(oldaddr, old_size);
    return;
  }
  if (addr != oldaddr) {
    // Moved (or freed, for size 0): a new block.
    register_allocfn(addr, new_size);
    return;
  }
  // In place: clear what it grew by. Without a recorded size, treat it as a
  // new block.
  if (!known)
    register_alloca(addr, new_size);
  else if (old_size < new_size)
    register_alloca(addr + old_size, new_size - old_size);
  heap_sizes.insert(addr, new_size);
}

void CilkpraceImpl_t::report_free_race(uintptr_t addr, csi_id_t free_id,
                                       free_site_t site) {
  const csan_source_loc_t *loc =
      site == free_site_t::free      ? __csan_get_free_source_loc(free_id)
      : site == free_site_t::realloc ? __csan_get_allocfn_source_loc(free_id)
                                     : __csan_get_call_source_loc(free_id);
  bool is_realloc =
      site == free_site_t::realloc || site == free_site_t::realloc_call;
  fprintf(stderr, "%s RACE ON BYTE %lx (+%zu)\n", is_realloc ? "REALLOC" : "FREE",
          (unsigned long)addr, shadow_mem.vmem_shadow_granularity);
  if (loc)
    fprintf(stderr, "@ %s Ln %d Col %d\n",
            loc->filename ? loc->filename : "<unknown>",
            loc->line_number, loc->column_number);
  fprintf(stderr, "======================\n");
  _exit(EXIT_FAILURE);
}

// Cilksan moves the frame's low end here; register_frame needs no low end.
void CilkpraceImpl_t::advance_stack_frame(uintptr_t addr) {}

// Not modeled: Cilksan treats the stack a stackrestore gives back as a write
// (in parallel code). Its hook runs after the restore, so finding that range
// needs the per-frame low end, which a parallel run doesn't have (see
// register_frame). The space is still cleared at its next alloca or frame.
void CilkpraceImpl_t::restore_stack(const csi_id_t call_id, uintptr_t addr) {}

#pragma GCC visibility pop

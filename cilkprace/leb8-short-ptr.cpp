#include "shadow_label.h"

#include <cstdio>
#include <cstdlib>

// See leb8-short-ptr.h.

// noinline keeps it, and its per-worker block, in the dylib.
__attribute__((noinline)) const os_label *leb8_short_ptr_store(const os_label &l) {
  constexpr size_t kRecord = 128, kBlock = size_t(1) << 20;
  static_assert(sizeof(os_label) <= kRecord, "a label must fit in a record");
  static thread_local char *cur = nullptr, *end = nullptr;
  if (cur == end) {
    cur = static_cast<char *>(aligned_alloc(kRecord, kBlock));
    if (!cur || reinterpret_cast<uintptr_t>(cur) + kBlock > (uint64_t(1) << 48)) {
      fprintf(stderr, "cilkprace: leb8-short-ptr cannot allocate label records\n");
      abort();
    }
    end = cur + kBlock;
  }
  os_label *r = reinterpret_cast<os_label *>(cur);
  *r = l;
  cur += kRecord;
  return r;
}

namespace {

constexpr unsigned kPtrBits = 41, kEndBits = 11;

__attribute__((always_inline)) inline const os_label *label_of(uint64_t w) {
  return reinterpret_cast<const os_label *>((w & ((uint64_t(1) << kPtrBits) - 1)) << 7);
}
__attribute__((always_inline)) inline unsigned end_of(uint64_t w) {
  return (w >> kPtrBits) & ((1u << kEndBits) - 1);
}
__attribute__((always_inline)) inline unsigned write_depth_of(uint64_t w) {
  return w >> (kPtrBits + kEndBits);
}
__attribute__((always_inline)) inline uint64_t encode(const os_label *l, unsigned end,
                                                      unsigned write_depth) {
  return reinterpret_cast<uintptr_t>(l) >> 7 | uint64_t(end) << kPtrBits |
         uint64_t(write_depth) << (kPtrBits + kEndBits);
}

// The strand's record, cached in the tool word after its label; null until
// the strand first stores its label.
__attribute__((always_inline)) inline const os_label *&cached(const os_label &cur) {
  return *reinterpret_cast<const os_label **>(const_cast<os_label *>(&cur) + 1);
}
__attribute__((always_inline)) inline const os_label *own_record(const os_label &cur) {
  const os_label *&c = cached(cur);
  if (!c)
    c = leb8_short_ptr_store(cur);
  return c;
}

// The LCA of the first end bits of *l (the entry may use a shorter prefix
// than the label's own) with cur. The empty entry's LCA with anything is 0.
__attribute__((always_inline)) inline unsigned lca(const os_label *l, unsigned end,
                                                   const os_label &cur) {
  if (!l)
    return 0;
  unsigned d = l->lca(cur);
  return d < end ? d : end;
}

// The entry is read with acquire so the record it names is visible, and
// published with release after the record was written.
__attribute__((always_inline)) inline uint64_t load(const uint64_t &entry) {
  return __atomic_load_n(&entry, __ATOMIC_ACQUIRE);
}
__attribute__((always_inline)) inline bool update(uint64_t &entry, uint64_t old,
                                                  uint64_t desired) {
  return __atomic_compare_exchange_n(&entry, &old, desired, false, __ATOMIC_RELEASE,
                                     __ATOMIC_RELAXED);
}

} // namespace

// leb8-single's does_read_race: the same-strand and widened-summary tests
// inline, the update out of line.
bool shadow_label::does_read_race(const os_label &reader) {
  const os_label *cur = cached(reader);
  uint64_t w = load(entry);
  const os_label *l = label_of(w);
  unsigned end = end_of(w), write_depth = write_depth_of(w);
  bool summary = (end & 3) == 3;
  // Entry is this strand's label, and no parallel write is recorded.
  if (!summary && l == cur && l && end == reader.end_idx && write_depth <= end &&
      write_depth % 4 != 3)
    return false;
  unsigned lca_depth = lca(l, end, reader);
  // Entry summarizes the parallel readers under a P node; a reader inside that
  // subtree adds nothing.
  if (summary && write_depth <= end && lca_depth >= end)
    return false;
  return does_read_race_slow(reader, w, lca_depth);
}

// leb8-single's locked read path, as a CAS of the entry w that was read.
__attribute__((noinline)) bool shadow_label::does_read_race_slow(const os_label &reader,
                                                                 uint64_t w,
                                                                 unsigned lca_depth) {
  const os_label *l = label_of(w);
  unsigned end = end_of(w), write_depth = write_depth_of(w);
  if (lca_depth < write_depth)
    write_depth = lca_depth;
  bool race = write_depth % 4 == 3;
  if (!race) {
    if (lca_depth % 4 != 3) {
      l = own_record(reader);
      end = reader.end_idx;
    } else if (lca_depth < end) {
      end = lca_depth;
    }
  }
  uint64_t nw = encode(l, end, write_depth);
  if (nw == w || update(entry, w, nw))
    return race;
  // Another check changed the entry since it was read: check again.
  return does_read_race(reader);
}

// leb8-single's does_write_race: the same-strand test inline, the rest out of
// line.
bool shadow_label::does_write_race(const os_label &writer) {
  const os_label *cur = cached(writer);
  uint64_t w = load(entry);
  if (cur && label_of(w) == cur && end_of(w) == writer.end_idx &&
      write_depth_of(w) >= writer.end_idx)
    return false;
  return does_write_race_slow(writer, w);
}

// leb8-single's locked write path, as a CAS of the entry w that was read.
__attribute__((noinline)) bool shadow_label::does_write_race_slow(const os_label &writer,
                                                                  uint64_t w) {
  const os_label *l = label_of(w);
  unsigned end = end_of(w), write_depth = write_depth_of(w);
  unsigned lca_depth = lca(l, end, writer);
  if (lca_depth < write_depth)
    write_depth = lca_depth;
  bool race = write_depth % 4 == 3 || lca_depth % 4 == 3;
  if (!race) {
    l = own_record(writer);
    end = write_depth = writer.end_idx;
  }
  uint64_t nw = encode(l, end, write_depth);
  if (nw == w || update(entry, w, nw))
    return race;
  return does_write_race(writer);
}

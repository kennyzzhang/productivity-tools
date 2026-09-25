#include "shadow_label.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>

// See leb8-ptr.h.

// Counting build for evaluating the design: -DCILKPRACE_PTR_COUNT=1 prints how
// checks resolve, how many labels the table holds, and how often the same
// pair of labels reaches the LCA. Run with one worker.
#ifndef CILKPRACE_PTR_COUNT
#define CILKPRACE_PTR_COUNT 0
#endif

leb8_ptr_record *leb8_ptr_table;

// Ids are handed out to each worker in blocks, so storing a label is a
// plain store almost always. Id 0 is the all-zero label of an empty entry.
static constexpr uint32_t kMaxRecords = 1u << 28;
static constexpr uint32_t kBlock = 4096;
static std::atomic<uint32_t> next_block{1};

#if CILKPRACE_PTR_COUNT
#include <unordered_set>
// Exported, not internal: the inlined checks run in the program, which would
// otherwise get its own copy.
struct counts_t {
  std::atomic<unsigned long long> reads{0}, writes{0}, read_same{0}, read_widened{0},
      write_same{0}, lcas{0}, lca_1word{0}, lca_2word{0}, lca_wider{0}, updates{0},
      stored{0}, sampled{0};
  std::unordered_set<uint64_t> sampled_pairs;
  ~counts_t() {
    unsigned long long checks = reads + writes;
    fprintf(stderr,
            "leb8-ptr: checks %llu (reads %llu, writes %llu)\n"
            "  resolved by id: reads %llu, writes %llu (%.1f%% of checks)\n"
            "  resolved by widened summary: %llu\n"
            "  LCAs %llu (%.1f%% of checks); widths: 1 word %llu, 2 words %llu, wider %llu\n"
            "  entry updates %llu\n"
            "  labels stored %llu (%.1f MB of table)\n"
            "  LCA pairs, 1/16 sample: %llu LCAs, %zu distinct pairs\n",
            checks, reads.load(), writes.load(), read_same.load(), write_same.load(),
            checks ? 100.0 * (read_same + write_same) / checks : 0.0,
            read_widened.load(), lcas.load(), checks ? 100.0 * lcas / checks : 0.0,
            lca_1word.load(), lca_2word.load(), lca_wider.load(), updates.load(),
            stored.load(), stored * sizeof(leb8_ptr_record) / 1e6, sampled.load(),
            sampled_pairs.size());
  }
};
__attribute__((visibility("default"))) counts_t leb8_ptr_counts;
#define COUNT(x) leb8_ptr_counts.x.fetch_add(1, std::memory_order_relaxed)
// Out of line and exported: the inlined check can only call into the dylib.
__attribute__((noinline, visibility("default"))) void leb8_ptr_sample(uint64_t key) {
  COUNT(sampled);
  leb8_ptr_counts.sampled_pairs.insert(key);
}
#else
#define COUNT(x) ((void)0)
#endif

// noinline keeps it, and its block counter, in the dylib.
__attribute__((noinline)) uint32_t leb8_ptr_store_label(const os_label &l) {
  static thread_local uint32_t cur = 0, end = 0;
  if (cur == end) {
    cur = next_block.fetch_add(kBlock, std::memory_order_relaxed);
    end = cur + kBlock;
    if (end > kMaxRecords) {
      fprintf(stderr, "cilkprace: leb8-ptr label table full (%u labels)\n", kMaxRecords);
      abort();
    }
  }
  COUNT(stored);
  leb8_ptr_table[cur].label = l;
  return cur++;
}

namespace {

// The strand's id lives complemented in the low half of the tool word after
// its label (see leb8-ptr.h), so the freshly reset word reads as ~0u: not in
// the table yet, and matching no entry. The top bit is the runtime's
// os_label::tool_word_deep.
__attribute__((always_inline)) inline uint64_t *id_word(const os_label &cur) {
  return reinterpret_cast<uint64_t *>(const_cast<os_label *>(&cur) + 1);
}

__attribute__((always_inline)) inline uint32_t store_current_label(const os_label &cur) {
  uint32_t id = leb8_ptr_store_label(cur);
  *id_word(cur) = (*id_word(cur) & os_label::tool_word_deep) | static_cast<uint32_t>(~id);
  return id;
}

// os_label::lca of the first a_end bits of a (a's own end_idx is its full
// length; the entry may use a shorter prefix), for a and b with the same
// anchor, so the same window base. Indices are window-relative, like end_idx.
__attribute__((always_inline)) inline unsigned lca(const os_label &a, unsigned a_end,
                                                   const os_label &b) {
  unsigned last_idx = b.end_idx < a_end ? b.end_idx : a_end;
#if CILKPRACE_PTR_COUNT
  COUNT(lcas);
  if (last_idx <= 64) COUNT(lca_1word);
  else if (last_idx <= 128) COUNT(lca_2word);
  else COUNT(lca_wider);
#endif
  if (last_idx < 64)
    return __builtin_ctzll((a.data[0] ^ b.data[0]) | (1ull << last_idx));
  if (a.data[0] != b.data[0])
    return __builtin_ctzll(a.data[0] ^ b.data[0]);
  if (last_idx == 64)
    return 64;
  if (last_idx < 128)
    return 64 + __builtin_ctzll((a.data[1] ^ b.data[1]) | (1ull << (last_idx - 64)));
  if (a.data[1] != b.data[1])
    return 64 + __builtin_ctzll(a.data[1] ^ b.data[1]);
  if (last_idx == 128)
    return 128;
  unsigned full_words = last_idx / 64;
  for (unsigned i = 2; i < full_words; i++)
    if (a.data[i] != b.data[i])
      return i * 64 + __builtin_ctzll(a.data[i] ^ b.data[i]);
  unsigned remain_bits = last_idx % 64;
  if (remain_bits == 0)
    return last_idx;
  return full_words * 64 +
         __builtin_ctzll((a.data[full_words] ^ b.data[full_words]) | (1ull << remain_bits));
}

__attribute__((always_inline)) inline unsigned lca_with(uint32_t id, unsigned end,
                                                        uint32_t cur_id,
                                                        const os_label &cur) {
#if CILKPRACE_PTR_COUNT
  if (cur_id != ~0u) {
    uint64_t key = static_cast<uint64_t>(id) << 32 | cur_id;
    if ((key * 0x9E3779B97F4A7C15ull) >> 60 == 0)
      leb8_ptr_sample(key);
  }
#endif
  return lca(leb8_ptr_table[id].label, end, cur);
}

// The entry is read with acquire so the table entry it names is visible, and
// published with release after the label was written to the table.
__attribute__((always_inline)) inline uint64_t load(const uint64_t &entry) {
  return __atomic_load_n(&entry, __ATOMIC_ACQUIRE);
}

__attribute__((always_inline)) inline bool update(uint64_t &entry, uint64_t old,
                                                  uint64_t desired) {
  COUNT(updates);
  return __atomic_compare_exchange_n(&entry, &old, desired, false, __ATOMIC_RELEASE,
                                     __ATOMIC_RELAXED);
}

// An entry in whole-label terms, for the out-of-line paths.
struct wide_state {
  uint32_t id;
  const os_label *label;
  uint32_t end, write_depth;
};

wide_state decode(uint64_t w) {
  uint32_t id = static_cast<uint32_t>(w);
  const leb8_ptr_record &r = leb8_ptr_table[id];
  unsigned end = static_cast<uint16_t>(w >> 32);
  if (end == kLeb8PtrWide)
    return {id, &r.label, r.end, r.write_depth};
  return {id, &r.label, r.label.base + (end & ~kLeb8PtrDeep),
          r.label.base + static_cast<unsigned>(w >> 48)};
}

// The whole-label LCA of entry e (read as w) and cur: the inline lca when they
// share a window, as the inline paths use, else the label's own.
unsigned entry_lca(const wide_state &e, uint64_t w, uint32_t cur_id, const os_label &cur) {
  unsigned end = static_cast<uint16_t>(w >> 32);
  if (end != kLeb8PtrWide && e.label->anchor == cur.anchor)
    return cur.base + lca_with(e.id, end & ~kLeb8PtrDeep, cur_id, cur);
  unsigned lca_depth = e.label->lca(cur);
  return lca_depth < e.end ? lca_depth : e.end;
}

// The entry for label id (whose label is l) with this end and write_depth:
// narrow if they fit relative to l's window, else a wide record. Wide records
// are immutable, so equal ones are shared through a small per-worker cache:
// a deep strand typically stores the same write_depth into many entries (say,
// reading data written near the root), and without it every such read would
// add a record.
uint64_t encode(uint32_t id, const os_label &l, uint32_t end, uint32_t write_depth) {
  if (write_depth >= l.base) // then end >= write_depth >= base, and end <= l.end()
    return id | static_cast<uint64_t>((end - l.base) | (l.anchor ? kLeb8PtrDeep : 0)) << 32 |
           static_cast<uint64_t>(write_depth - l.base) << 48;
  struct slot {
    uint32_t id, end, write_depth, wide;
  };
  static thread_local slot cache[256];
  slot &c = cache[(id * 0x9E3779B1u ^ end * 0x85EBCA77u ^ write_depth) % 256];
  if (c.wide == 0 || c.id != id || c.end != end || c.write_depth != write_depth) {
    uint32_t wide = leb8_ptr_store_label(l);
    leb8_ptr_table[wide].end = end;
    leb8_ptr_table[wide].write_depth = write_depth;
    c = {id, end, write_depth, wide};
  }
  return c.wide | static_cast<uint64_t>(kLeb8PtrWide) << 32;
}

// The strand's own label, from the table.
__attribute__((always_inline)) inline uint32_t own_id(uint32_t cur_id, const os_label &cur) {
  return cur_id == ~0u ? store_current_label(cur) : cur_id;
}

} // namespace

__attribute__((always_inline)) inline uint64_t encode_narrow(uint32_t id, unsigned end,
                                                             unsigned write_depth) {
  return id | static_cast<uint64_t>(end) << 32 | static_cast<uint64_t>(write_depth) << 48;
}

// leb8-single's does_read_race: the same-strand and widened-summary tests
// inline, the update out of line.
bool shadow_label::does_read_race(const os_label &reader) {
  COUNT(reads);
  uint64_t tool_word = *id_word(reader);
  uint32_t cur_id = ~static_cast<uint32_t>(tool_word);
  uint64_t w = load(entry);
  uint32_t id = static_cast<uint32_t>(w);
  // With the deep bit: see the summary test below.
  unsigned end = static_cast<uint16_t>(w >> 32);
  unsigned write_depth = w >> 48;
  bool summary = (end & 3) == 3;
  // Entry is a single strand's label. If it is ours and no parallel write is
  // recorded, nothing would change. Same id, so same window base.
  if (!summary && id == cur_id && (end & ~kLeb8PtrDeep) == reader.end_idx &&
      write_depth <= end && write_depth % 4 != 3) {
    COUNT(read_same);
    return false;
  }
  // Only meaningful if both labels are within their windows; see below.
  unsigned lca_depth = lca_with(id, end, cur_id, reader);
  // Entry summarizes the parallel readers under a P node; a reader inside that
  // subtree adds nothing. An entry that is deep or wide never passes, as its
  // end has the deep bit and the LCA is at most the reader's end_idx, so it
  // goes out of line, which then ignores lca_depth. A deep reader is excluded
  // by the tool word's bit.
  if (summary && write_depth <= end && lca_depth >= end &&
      !(tool_word & os_label::tool_word_deep)) {
    COUNT(read_widened);
    return false;
  }
  return does_read_race_slow(reader, cur_id, w, lca_depth);
}

// The update paths for an entry or accessor whose label has left its window,
// in whole-label terms. Out of line on their own, so the shallow paths below
// stay as lean as leb8-ptr's were before labels had windows. Each returns the
// race verdict, or -1 if another check changed the entry first.
__attribute__((noinline)) static int read_update_deep(uint64_t &entry, const os_label &reader,
                                                      uint32_t cur_id, uint64_t w) {
  wide_state e = decode(w);
  unsigned lca_depth = entry_lca(e, w, cur_id, reader);
  unsigned end = e.end, write_depth = e.write_depth;
  if (lca_depth < write_depth)
    write_depth = lca_depth;
  bool race = write_depth % 4 == 3;
  uint32_t id = e.id;
  if (!race) {
    if (lca_depth % 4 != 3) {
      id = own_id(cur_id, reader);
      end = reader.end();
    } else if (lca_depth < end) {
      end = lca_depth;
    }
  }
  if (id == e.id && end == e.end && write_depth == e.write_depth)
    return race;
  if (update(entry, w, encode(id, leb8_ptr_table[id].label, end, write_depth)))
    return race;
  return -1;
}

__attribute__((noinline)) static int write_update_deep(uint64_t &entry, const os_label &writer,
                                                       uint32_t cur_id, uint64_t w) {
  wide_state e = decode(w);
  unsigned lca_depth = entry_lca(e, w, cur_id, writer);
  unsigned end = e.end, write_depth = e.write_depth;
  if (lca_depth < write_depth)
    write_depth = lca_depth;
  bool race = write_depth % 4 == 3 || lca_depth % 4 == 3;
  uint32_t id = e.id;
  if (!race) {
    id = own_id(cur_id, writer);
    end = write_depth = writer.end();
  }
  if (id == e.id && end == e.end && write_depth == e.write_depth)
    return race;
  if (update(entry, w, encode(id, leb8_ptr_table[id].label, end, write_depth)))
    return race;
  return -1;
}

// leb8-single's locked read path, as a CAS of the entry w that was read. When
// both labels are within their windows, whole-label and relative indices are
// the same, the inline path's lca_depth holds, and every result is narrow.
__attribute__((noinline)) bool shadow_label::does_read_race_slow(const os_label &reader,
                                                                 uint32_t cur_id, uint64_t w,
                                                                 unsigned lca_depth) {
  if ((w >> 32 & kLeb8PtrDeep) || reader.anchor) {
    int race = read_update_deep(entry, reader, cur_id, w);
    if (race >= 0)
      return race;
  } else {
    uint32_t id = static_cast<uint32_t>(w);
    unsigned end = static_cast<uint16_t>(w >> 32);
    unsigned write_depth = w >> 48;
    if (lca_depth < write_depth)
      write_depth = lca_depth;
    bool race = write_depth % 4 == 3;
    if (!race) {
      if (lca_depth % 4 != 3) {
        id = own_id(cur_id, reader);
        end = reader.end_idx;
      } else if (lca_depth < end) {
        end = lca_depth;
      }
    }
    uint64_t nw = encode_narrow(id, end, write_depth);
    if (nw == w || update(entry, w, nw))
      return race;
  }
  // Another check changed the entry since it was read: check again.
  return does_read_race(reader);
}

// leb8-single's does_write_race: the same-strand test inline, the rest out of
// line.
bool shadow_label::does_write_race(const os_label &writer) {
  COUNT(writes);
  uint32_t cur_id = ~static_cast<uint32_t>(*id_word(writer));
  uint64_t w = load(entry);
  unsigned end = writer.end_idx;
  if (static_cast<uint32_t>(w) == cur_id &&
      (static_cast<uint16_t>(w >> 32) & ~kLeb8PtrDeep) == end && (w >> 48) >= end) {
    COUNT(write_same);
    return false;
  }
  return does_write_race_slow(writer, cur_id, w);
}

// leb8-single's locked write path, as a CAS of the entry w that was read.
__attribute__((noinline)) bool shadow_label::does_write_race_slow(const os_label &writer,
                                                                  uint32_t cur_id, uint64_t w) {
  uint32_t id = static_cast<uint32_t>(w);
  unsigned end = static_cast<uint16_t>(w >> 32);
  if ((end & kLeb8PtrDeep) || writer.anchor) {
    int race = write_update_deep(entry, writer, cur_id, w);
    if (race >= 0)
      return race;
  } else {
    // Both labels within their windows, as in does_read_race_slow.
    unsigned write_depth = w >> 48;
    unsigned lca_depth = lca_with(id, end, cur_id, writer);
    if (lca_depth < write_depth)
      write_depth = lca_depth;
    bool race = write_depth % 4 == 3 || lca_depth % 4 == 3;
    if (!race) {
      id = own_id(cur_id, writer);
      end = write_depth = writer.end_idx;
    }
    uint64_t nw = encode_narrow(id, end, write_depth);
    if (nw == w || update(entry, w, nw))
      return race;
  }
  return does_write_race(writer);
}

void leb8_ptr_init() {
  void *table = mmap(nullptr, size_t(kMaxRecords) * sizeof(leb8_ptr_record),
                     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
  if (table == MAP_FAILED) {
    perror("cilkprace: leb8-ptr label table");
    abort();
  }
  leb8_ptr_table = static_cast<leb8_ptr_record *>(table);
}

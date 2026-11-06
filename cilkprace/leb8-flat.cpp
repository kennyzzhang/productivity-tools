#include "shadow_label.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/resource.h>
#include <unistd.h>

// See leb8-flat.h.

using namespace leb8_flat;
using fields = leb8_flat::fields_t;

// Memory counters: block chunks and peak RSS, printed at exit.
static std::atomic<unsigned long long> flat_chunks{0};
static struct flat_report_t {
  ~flat_report_t() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    double rss = ru.ru_maxrss / 1e6;
#else
    double rss = ru.ru_maxrss / 1e3;
#endif
    fprintf(stderr, "MEMCOUNT flat block_chunks=%llu (%.1f MB) peak_rss=%.1f MB\n",
            flat_chunks.load(), flat_chunks * 1.048576, rss);
  }
} flat_report;

namespace {

// --- Labels ----------------------------------------------------------------

// Word i (bits [64i, 64i + 64)) of the whole label: os_label::word_at, which
// lives in the runtime library, inline here.
inline uint64_t label_word(const os_label &l, unsigned i) {
  unsigned b = l.base / 64;
  if (i >= b)
    return i - b < CILKPRACE_ANCHOR_WORDS ? l.data[i - b] : 0;
  for (const os_label_chunk *c = l.anchor; c; c = c->parent)
    if (i >= c->base / 64)
      return i - c->base / 64 < c->nwords ? c->bits[i - c->base / 64] : 0;
  return 0;
}

// The LCA of the first `last` bits (last <= kBits) of entry words e with
// cur's. A label with base 0 is all in its window, and last <= cur.end() keeps
// the index inside it.
__attribute__((always_inline)) inline unsigned lca_words(const uint64_t *e, unsigned last,
                                                         const os_label &cur) {
  unsigned full = last / 64;
  for (unsigned i = 0; i < full; ++i) {
    uint64_t c = cur.base ? label_word(cur, i) : cur.data[i];
    if (e[i] != c)
      return i * 64 + __builtin_ctzll(e[i] ^ c);
  }
  unsigned rem = last % 64;
  if (!rem)
    return last;
  uint64_t c = cur.base ? label_word(cur, full) : cur.data[full];
  return full * 64 + __builtin_ctzll((e[full] ^ c) | (1ull << rem));
}

// The LCA of the entry's label, cut to its end, with cur. The empty entry's
// LCA with anything is 0.
__attribute__((always_inline)) inline unsigned entry_lca(const fields &s, const os_label &cur) {
  unsigned ce = cur.end(), last = ce < s.end ? ce : s.end;
  return lca_words(s.w, last, cur);
}

// Whether the entry holds exactly cur's label, to cur's own end.
__attribute__((always_inline)) inline bool same_label(const fields &s, const os_label &cur) {
  return s.end == cur.end() && lca_words(s.w, s.end, cur) >= s.end;
}

// The entry with cur's label, end e and write depth wd (under the lock).
__attribute__((noinline)) void label_too_long(const os_label &l) {
  fprintf(stderr,
          "cilkprace: leb8-flat holds labels of at most %u bits; a strand's label is %u bits "
          "(too many spawns without a sync)\n",
          kBits, l.end());
  // Not abort(): a core dump of the reserved shadow (tens of TB of address
  // space) runs for hours, and a piped core_pattern ignores RLIMIT_CORE.
  // _exit also skips exit handlers, which could block on the lock held here.
  fflush(stderr);
  _exit(1);
}
inline fields with_label(const os_label &cur, unsigned e, unsigned wd) {
  if (CILKPRACE_UNLIKELY(cur.end() > kBits))
    label_too_long(cur);
  fields s;
  for (unsigned i = 0; i < kWords; ++i)
    s.w[i] = label_word(cur, i);
  s.end = uint16_t(e);
  s.write_depth = uint16_t(wd);
  return s;
}
// The entry with its own label, and end e and write depth wd (under the lock).
inline fields with_ends(fields s, unsigned e, unsigned wd) {
  s.end = uint16_t(e);
  s.write_depth = uint16_t(wd);
  return s;
}

// The same-strand and widened-summary tests: the access neither races nor
// changes the entry.
__attribute__((always_inline)) inline bool read_keeps(const fields &s, const os_label &cur) {
  if (s.write_depth > s.end)
    return false;
  if ((s.end & 3) == 3)
    return entry_lca(s, cur) >= s.end;
  return s.write_depth % 4 != 3 && same_label(s, cur);
}
__attribute__((always_inline)) inline bool write_keeps(const fields &s, const os_label &cur) {
  return s.write_depth >= cur.end() && same_label(s, cur);
}

} // namespace

// --- Whole-entry checks -----------------------------------------------------
// The fields are read with plain loads under the seqlock, as in leb8-single:
// a snapshot is only trusted once read_was_safe confirms no writer intervened.

bool shadow_label::does_read_race(const os_label &reader) {
  uint32_t q = seqlock.begin_read();
  fields s = f;
#if CILKPRACE_ADAPTIVE_GRANULE
  if (s.mode != kPlain)
    return does_read_race_block(reader);
#endif
  if (read_keeps(s, reader) && CILKPRACE_LIKELY(seqlock.read_was_safe(q)))
    return false;
  return does_read_race_slow(reader);
}

bool shadow_label::does_write_race(const os_label &writer) {
  uint32_t q = seqlock.begin_read();
  fields s = f;
#if CILKPRACE_ADAPTIVE_GRANULE
  if (s.mode != kPlain)
    return does_write_race_block(writer);
#endif
  if (write_keeps(s, writer) && CILKPRACE_LIKELY(seqlock.read_was_safe(q)))
    return false;
  return does_write_race_slow(writer);
}

namespace {
// leb8-single's locked read and write updates of entry e, which the caller
// holds a lock on (its own seqlock, or its line's).
inline bool read_update(fields &f, const os_label &reader) {
  fields s = f;
  unsigned e = s.end, wd = s.write_depth, lca_depth = entry_lca(s, reader);
  bool race = false;
  if ((e & 3) == 3 && wd <= e && lca_depth >= e) {
    // A summary of parallel readers this reader is inside: nothing to do.
  } else {
    if (lca_depth < wd)
      wd = lca_depth;
    race = wd % 4 == 3;
    if (!race && lca_depth % 4 != 3)
      f = with_label(reader, reader.end(), wd);
    else
      f = with_ends(s, !race && lca_depth < e ? lca_depth : e, wd);
  }
  return race;
}
inline bool write_update(fields &f, const os_label &writer) {
  fields s = f;
  bool race = false;
  if (!write_keeps(s, writer)) {
    unsigned wd = s.write_depth, lca_depth = entry_lca(s, writer);
    if (lca_depth < wd)
      wd = lca_depth;
    race = wd % 4 == 3 || lca_depth % 4 == 3;
    f = race ? with_ends(s, s.end, wd) : with_label(writer, writer.end(), writer.end());
  }
  return race;
}
} // namespace

// leb8-single's locked read path.
__attribute__((noinline)) bool shadow_label::does_read_race_slow(const os_label &reader) {
  seqlock.begin_write();
#if CILKPRACE_ADAPTIVE_GRANULE
  if (f.mode != kPlain) { // split since the inline path looked
    seqlock.end_write();
    return does_read_race_block(reader);
  }
#endif
  bool race = read_update(f, reader);
  seqlock.end_write();
  return race;
}

// leb8-single's locked write path.
__attribute__((noinline)) bool shadow_label::does_write_race_slow(const os_label &writer) {
  seqlock.begin_write();
#if CILKPRACE_ADAPTIVE_GRANULE
  if (f.mode != kPlain) {
    seqlock.end_write();
    return does_write_race_block(writer);
  }
#endif
  bool race = write_update(f, writer);
  seqlock.end_write();
  return race;
}

#if CILKPRACE_ADAPTIVE_GRANULE
// --- Adaptive lines ---------------------------------------------------------
// A refined line is one level deep: w[0] points at a block of plain child
// entries of 2^aux bytes each, kLine >> aux of them, sized by the finest
// access the line has seen. Children have no lock of their own; the line's
// seqlock covers its block. A check reads children optimistically under a
// validated snapshot of the line and re-validates the line's seqlock before
// answering "no race"; an update, and so any race, it does under the line's
// write lock after checking the line is as snapshotted, so it never writes
// to a block the line no longer owns. A finer access resplits the line: a new
// block is built from copies of the old children and the old block is
// retired on the line's chain through w[1], since lockless readers may still
// be on it, to be recycled with the line (clear or merge).

namespace {
constexpr unsigned kLine = CILKPRACE_GRANULARITY;
constexpr unsigned kLgLine = __builtin_ctz(kLine);
static_assert(kLine >= 2 && kLine <= 128 && (kLine & (kLine - 1)) == 0,
              "a line is 2 to 128 bytes, a power of 2");

// A child entry: the fields alone, packed. Neighbouring children share cache
// lines as the program bytes they shadow do, so the shadow's contention and
// storage follow the program's.
struct child {
  fields f;
};
static_assert(sizeof(child) == 56, "a child is 56 bytes");

inline unsigned child_lg(const fields &s) { return s.aux; }
inline unsigned fanout_lg(const fields &s) { return kLgLine - s.aux; }
inline child *block_of(const fields &s) { return reinterpret_cast<child *>(s.w[0]); }
inline child *retired_of(const fields &s) { return reinterpret_cast<child *>(s.w[1]); }

inline bool same_fields(const fields &a, const fields &b) {
  for (unsigned i = 0; i < kWords; ++i)
    if (a.w[i] != b.w[i])
      return false;
  return a.end == b.end && a.write_depth == b.write_depth && a.mode == b.mode && a.aux == b.aux;
}
inline bool is_empty(const fields &t) {
  for (unsigned i = 0; i < kWords; ++i)
    if (t.w[i])
      return false;
  return t.end == 0 && t.write_depth == 0 && t.mode == kPlain;
}

// Per-worker blocks of 2^flg children (flg 1 to kLgLine), carved from 1 MB
// chunks, and a per-worker free list for each size, linked through w[0]. A
// line refined to bytes costs 128 * 56 B = 7 KB.
thread_local child *free_blocks[kLgLine + 1] = {};

__attribute__((noinline)) child *new_block(unsigned flg) {
  child *&fl = free_blocks[flg];
  if (child *b = fl) {
    fl = reinterpret_cast<child *>(b->f.w[0]);
    return b;
  }
  constexpr size_t kChunk = size_t(1) << 20;
  static thread_local char *cur = nullptr, *end = nullptr;
  size_t bytes = sizeof(child) << flg;
  if (cur + bytes > end) {
    cur = static_cast<char *>(aligned_alloc(64, kChunk));
    if (!cur) {
      fprintf(stderr, "cilkprace: cannot allocate shadow blocks\n");
      abort();
    }
    flat_chunks.fetch_add(1, std::memory_order_relaxed);
    end = cur + kChunk;
  }
  child *b = reinterpret_cast<child *>(cur);
  cur += bytes;
  return b;
}
inline void free_block(child *b, unsigned flg) {
  b->f.w[0] = reinterpret_cast<uint64_t>(free_blocks[flg]);
  free_blocks[flg] = b;
}
// Frees refined line t's block and its retired blocks. A retired block's
// first child holds the next one in w[1] and the block's fanout lg in aux.
void free_line(const fields &t) {
  for (child *b = retired_of(t); b;) {
    child *next = retired_of(b[0].f);
    free_block(b, b[0].f.aux);
    b = next;
  }
  free_block(block_of(t), fanout_lg(t));
}

// The child size for an access to bytes [lo, hi) of the line, not all of
// it: the largest the access covers whole.
inline unsigned access_lg(unsigned lo, unsigned hi) { return __builtin_ctz(lo | hi); }

inline fields refined(child *b, unsigned clg, child *retired) {
  fields t;
  t.w[0] = reinterpret_cast<uint64_t>(b);
  t.w[1] = reinterpret_cast<uint64_t>(retired);
  t.mode = kRefined;
  t.aux = uint8_t(clg);
  return t;
}

// Under the line's write lock: splits plain line s into children of 2^clg
// bytes, copies of it.
inline fields split(shadow_label &s, const fields &old, unsigned clg) {
  unsigned flg = kLgLine - clg;
  child *b = new_block(flg);
  for (size_t j = 0; j < size_t(1) << flg; ++j)
    b[j].f = old;
  fields t = refined(b, clg, nullptr);
  s.f = t;
  return t;
}
// Under the line's write lock: resplits refined line s to children of 2^clg
// bytes, clg < child_lg(t), each old child copied into the new ones it
// covered, and retires the old block. A lockless reader still on it sees
// fields that only ever send it to the locked path or fail its validation.
inline fields resplit(shadow_label &s, const fields &t, unsigned clg) {
  unsigned per = 1u << (child_lg(t) - clg), oflg = fanout_lg(t);
  child *ob = block_of(t), *b = new_block(kLgLine - clg);
  for (size_t i = 0, n = size_t(1) << oflg; i < n; ++i)
    for (unsigned k = 0; k < per; ++k)
      b[i * per + k].f = ob[i].f;
  ob[0].f.w[1] = t.w[1];
  ob[0].f.aux = uint8_t(oflg);
  fields u = refined(b, clg, ob);
  s.f = u;
  return u;
}
// Under the line's write lock: brings line s to children of at most 2^alg
// bytes and returns its fields.
inline fields refine_to(shadow_label &s, unsigned alg) {
  fields t = s.f;
  if (t.mode != kRefined)
    return split(s, t, alg);
  if (child_lg(t) > alg)
    return resplit(s, t, alg);
  return t;
}

template <bool Write>
inline bool keeps(const fields &c, const os_label &cur) {
  return Write ? write_keeps(c, cur) : read_keeps(c, cur);
}
template <bool Write>
inline bool update(fields &c, const os_label &cur) {
  return Write ? write_update(c, cur) : read_update(c, cur);
}

// Checks children [i, e) of refined line s against its snapshot t, read at
// seqlock value q: 0 no race, 1 the line changed (snapshot it again), 2 + k a
// race on child k. An optimistic pass first; if any child needs an update,
// the locked pass over the same children, every one: the optimistic reads
// of the children it passed were never validated, and a race the tool
// ignores must not hide a later one.
template <bool Write>
unsigned check_children(shadow_label &s, uint32_t q, const fields &t, unsigned i, unsigned e,
                        const os_label &cur) {
  child *b = block_of(t);
  unsigned j = i;
  while (j < e && keeps<Write>(b[j].f, cur))
    ++j;
  if (j == e)
    return s.seqlock.read_was_safe(q) ? 0 : 1;
  unsigned r = 1;
  s.seqlock.begin_write();
  if (same_fields(s.f, t)) {
    r = 0;
    for (j = i; j < e; ++j)
      if (!keeps<Write>(b[j].f, cur) && update<Write>(b[j].f, cur) && r == 0)
        r = 2 + j;
  }
  s.seqlock.end_write();
  return r;
}

// All of line s.
template <bool Write>
bool check_all(shadow_label &s, const os_label &cur) {
  for (;;) {
    uint32_t q = s.seqlock.begin_read();
    fields t = s.f;
    if (!s.seqlock.read_was_safe(q))
      continue;
    if (t.mode != kRefined) // merged back meanwhile
      return Write ? s.does_write_race(cur) : s.does_read_race(cur);
    unsigned r = check_children<Write>(s, q, t, 0, 1u << fanout_lg(t), cur);
    if (r != 1)
      return r != 0;
  }
}

// Bytes [off, off + n) of the line, when they cover whole children of a
// refined line or leave an unrefined line as it is: 0 no race, 1 not
// handled (use the _part check), 2 + k a race on byte k.
template <bool Write>
unsigned line_check(shadow_label &s, const os_label &cur, unsigned off, unsigned n) {
  if (off + n > kLine)
    return 1;
  for (;;) {
    uint32_t q = s.seqlock.begin_read();
    fields t = s.f;
    if (!s.seqlock.read_was_safe(q))
      continue;
    if (t.mode != kRefined)
      return keeps<Write>(t, cur) ? 0 : 1;
    unsigned clg = child_lg(t);
    if ((off | n) & ((1u << clg) - 1))
      return 1; // finer than the children: the _part check resplits
    unsigned r = check_children<Write>(s, q, t, off >> clg, (off + n) >> clg, cur);
    if (r == 0)
      return 0;
    if (r >= 2)
      return 2 + ((r - 2) << clg);
  }
}

// Bytes [lo, hi) of the line, not all of it: the offset of the first racing
// byte plus 1, or 0. An access that would change an unrefined line splits
// it; one finer than the children resplits.
template <bool Write>
unsigned part_check(shadow_label &s, unsigned lo, unsigned hi, const os_label &cur) {
  unsigned alg = access_lg(lo, hi);
  for (;;) {
    uint32_t q = s.seqlock.begin_read();
    fields t = s.f;
    if (!s.seqlock.read_was_safe(q))
      continue;
    if (t.mode != kRefined && keeps<Write>(t, cur))
      return 0;
    if (t.mode != kRefined || child_lg(t) > alg) {
      s.seqlock.begin_write();
      refine_to(s, alg);
      s.seqlock.end_write();
      continue;
    }
    unsigned clg = child_lg(t);
    unsigned r = check_children<Write>(s, q, t, lo >> clg, hi >> clg, cur);
    if (r == 0)
      return 0;
    if (r >= 2)
      return ((r - 2) << clg) + 1;
  }
}

// Empties bytes [lo, hi) of the line, not all of it.
void clear_part(shadow_label &s, unsigned lo, unsigned hi) {
  unsigned alg = access_lg(lo, hi);
  s.seqlock.begin_write();
  fields t = s.f;
  if (t.mode == kRefined || !is_empty(t)) {
    t = refine_to(s, alg);
    unsigned clg = child_lg(t);
    child *b = block_of(t);
    for (unsigned i = lo >> clg, e = hi >> clg; i < e; ++i)
      b[i].f = fields{};
  }
  s.seqlock.end_write();
}
// Empties the whole line. Only a racy program accesses it meanwhile.
inline void clear_line(shadow_label &s) {
  if (s.f.mode == kRefined)
    free_line(s.f);
  s.f = fields{};
}
} // namespace

__attribute__((noinline)) unsigned shadow_label::does_read_race_part(const os_label &reader,
                                                                     unsigned lo, unsigned hi) {
  return part_check<false>(*this, lo, hi, reader);
}
__attribute__((noinline)) unsigned shadow_label::does_write_race_part(const os_label &writer,
                                                                      unsigned lo, unsigned hi) {
  return part_check<true>(*this, lo, hi, writer);
}
__attribute__((noinline)) bool shadow_label::does_read_race_block(const os_label &reader) {
  return check_all<false>(*this, reader);
}
__attribute__((noinline)) bool shadow_label::does_write_race_block(const os_label &writer) {
  return check_all<true>(*this, writer);
}
unsigned shadow_label::does_read_race_line(const os_label &reader, unsigned off, unsigned n) {
  return line_check<false>(*this, reader, off, n);
}
unsigned shadow_label::does_write_race_line(const os_label &writer, unsigned off, unsigned n) {
  return line_check<true>(*this, writer, off, n);
}

// Merges refined line s back into one entry if all its children hold the
// same one. Called after a long write covered the line, so in a race-free
// program nothing else uses its blocks.
__attribute__((noinline)) void leb8_flat_merge(shadow_label &s) {
  for (;;) {
    uint32_t q = s.seqlock.begin_read();
    fields t = s.f;
    if (!s.seqlock.read_was_safe(q))
      continue;
    if (t.mode != kRefined)
      return;
    child *b = block_of(t);
    fields c = b[0].f;
    for (size_t i = 1, n = size_t(1) << fanout_lg(t); i < n; ++i)
      if (!same_fields(b[i].f, c))
        return;
    if (!s.seqlock.read_was_safe(q))
      continue;
    s.seqlock.begin_write();
    bool same = same_fields(s.f, t);
    if (same)
      s.f = c;
    s.seqlock.end_write();
    if (same)
      free_line(t);
    return;
  }
}

__attribute__((noinline)) void leb8_flat_clear(shadow_label *s, uintptr_t beg, size_t n) {
  if (n == 0) // alloca(0): nothing to clear, and no reason to split a line
    return;
  uintptr_t end = beg + n;
  if (beg % kLine) {
    uintptr_t line = beg / kLine * kLine;
    if (end - line < kLine) {
      clear_part(*s, beg - line, end - line);
      return;
    }
    clear_part(*s++, beg - line, kLine);
    beg = line + kLine;
  }
  size_t lines = (end - beg) / kLine;
  // Only the fields: a racy program may be mid-write on one of these entries,
  // and zeroing its seqlock would leave it odd forever once that write ends.
  for (size_t i = 0; i < lines; ++i)
    clear_line(s[i]);
  beg += lines * kLine;
  if (beg < end)
    clear_part(s[lines], 0, end - beg);
}
#endif // CILKPRACE_ADAPTIVE_GRANULE

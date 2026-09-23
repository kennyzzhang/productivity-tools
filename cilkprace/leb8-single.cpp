#include "shadow_label.h"

// An inline is_identical check was once removed from here for costing 16% at 1
// worker. That check ran unconditionally, called is_identical out of line in
// the opencilk dylib, and grew does_read_race past the inliner's threshold.
// The same-strand clause below runs only when the widen test cannot apply
// (end_idx % 4 != 3), is_identical is now inline, and the whole read path is
// always_inline (see leb8-single.h and cilkprace.h).
bool shadow_label::does_read_race(const os_label &reader) {
#if CILKPRACE_ABL_READ_WIDEN_FASTPATH || CILKPRACE_ABL_READ_IDENT_FASTPATH
  // Everything read here is speculative until read_was_safe() confirms no
  // writer intervened, so every early return must go through it.
  uint32_t seq = seqlock.begin_read();
  if ((active_reader.end_idx & 3) == 3) {
#if CILKPRACE_ABL_READ_WIDEN_FASTPATH
    // Entry summarizes the parallel readers under a P node; a reader inside
    // that subtree adds nothing.
    if (write_depth <= active_reader.end_idx &&
        active_reader.lca(reader) >= active_reader.end_idx &&
        CILKPRACE_LIKELY(seqlock.read_was_safe(seq))) {
      return false;
    }
#endif
  } else {
#if CILKPRACE_ABL_READ_IDENT_FASTPATH
    // Entry is a single strand's label. If it is ours, and write_depth shows no
    // parallel write, the locked path in does_read_race_slow would report
    // nothing and change nothing (it reassigns the same label). The write_depth
    // tests matter: an identical reader still races with a parallel write, so
    // is_identical alone would drop read-write races. Cheapest tests first; is_identical compares end_idx before any words. A
    // hand-rolled masked two-word compare timed the same and misses wider
    // labels (a fifth of nqueens' reads), so this uses the real thing.
    if (write_depth <= active_reader.end_idx && write_depth % 4 != 3 &&
        active_reader.is_identical(reader) &&
        CILKPRACE_LIKELY(seqlock.read_was_safe(seq))) {
      return false;
    }
#endif
  }
#endif
  return does_read_race_slow(reader);
}

bool shadow_label::does_write_race(const os_label &writer) {
#if CILKPRACE_ABL_WRITE_FASTPATH
  uint32_t seq = seqlock.begin_read();
  if (CILKPRACE_LIKELY((write_depth >= writer.end_idx) &&
                       active_reader.is_identical(writer))) {
    if (CILKPRACE_LIKELY(seqlock.read_was_safe(seq))) {
      return false;
    }
  }
#endif
  return does_write_race_slow(writer);
}

bool shadow_label::does_read_race_slow(const os_label &reader) {
  // Contended read or non-identical: check parallel ancestor or retry before write lock.
  uint32_t seq;
  bool no_race = false;
  // Everything read in here is speculative until read_was_safe() confirms no
  // writer intervened, so this loop only computes `no_race` -- it must not
  // return or break out early, or a torn read of active_reader/write_depth
  // could suppress a real race.
  do {
    seq = seqlock.begin_read();
    no_race = false;
    if ((active_reader.end_idx % 4 == 3) &&
        (write_depth <= active_reader.end_idx)) {
      no_race = active_reader.lca(reader) >= active_reader.end_idx;
    }
  } while (CILKPRACE_UNLIKELY(!seqlock.read_was_safe(seq)));

  if (no_race) {
    return false;
  }

  auto write_lock = seqlock.write_lock();
  std::lock_guard<decltype(write_lock)> lock(write_lock);

  unsigned lca_depth = active_reader.lca(reader);
  if (lca_depth < write_depth) {
    write_depth = lca_depth;
  }
  if (write_depth % 4 == 3) {
    return true;
  }
  if (lca_depth % 4 != 3) {
    // Plain assignment on purpose, unlike leb8-range's copy_from: on arm64
    // this 56-byte copy is three loads and three stores with no branches. A
    // copy of only the words in use adds branches on end_idx and timed 0.994x.
    active_reader = reader;
  } else if (lca_depth < active_reader.end_idx) {
    active_reader.end_idx = lca_depth;
  }
  return false;
}

bool shadow_label::does_write_race_slow(const os_label &writer) {
  uint32_t seq;
  bool is_same_writer = false;
  do {
    seq = seqlock.begin_read();
    is_same_writer = (write_depth >= writer.end_idx) && active_reader.is_identical(writer);
  } while (CILKPRACE_UNLIKELY(!seqlock.read_was_safe(seq)));

  if (is_same_writer) {
    return false;
  }

  auto write_lock = seqlock.write_lock();
  std::lock_guard<decltype(write_lock)> lock(write_lock);

  unsigned lca_depth = active_reader.lca(writer);
  if (lca_depth < write_depth) {
    write_depth = lca_depth;
  }
  if (write_depth % 4 == 3) {
    return true;
  }
  if (lca_depth % 4 != 3) {
    active_reader = writer;  // plain assignment; see the read path above
    write_depth = writer.end_idx;
  } else {
    return true;
  }
  return false;
}


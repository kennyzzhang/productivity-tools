#include "shadow_label.h"

// Deliberately no inline `active_reader.is_identical(reader)` check here.
// does_read_race is inlined into every instrumented load, and that check made
// it fat enough to cost 16% at 1 worker and 12% at 10 across the benchmark set
// -- it also hid most of the value of the other optimizations, several of which
// roughly doubled once it was gone. does_read_race_slow already re-checks
// is_identical under the seqlock, so nothing is lost but the inline attempt.
// Measured by tools/ablate.py; nqueens is the one benchmark that preferred it.
bool shadow_label::does_read_race(const os_label &reader) {
#if CILKPRACE_ABL_READ_WIDEN_FASTPATH
  uint32_t seq = seqlock.begin_read();
  if ((active_reader.end_idx & 3) == 3 && write_depth <= active_reader.end_idx) {
    if (active_reader.lca(reader) >= active_reader.end_idx) {
      if (CILKPRACE_LIKELY(seqlock.read_was_safe(seq))) {
        return false;
      }
    }
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
  do {
    seq = seqlock.begin_read();
    if (CILKPRACE_LIKELY(active_reader.is_identical(reader))) {
      no_race = true;
      break;
    }
    if ((active_reader.end_idx % 4 == 3) && (write_depth <= active_reader.end_idx)) {
      unsigned lca_depth = active_reader.lca(reader);
      if (lca_depth >= active_reader.end_idx) {
        no_race = true;
        break;
      }
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
    active_reader = writer;
    write_depth = writer.end_idx;
  } else {
    return true;
  }
  return false;
}


#include "shadow_label.h"

bool shadow_label::does_read_race_slow(const os_label &reader) {
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

bool shadow_label::does_read_race(const os_label &reader) {
  uint32_t seq;
  bool no_updates = false;

  do {
    seq = seqlock.begin_read();

    // Fast-path 1: Exactly same reader
    if (__builtin_expect(active_reader.is_identical(reader), 1)) {
      no_updates = true;
      break;
    }

    // Fast-path 2: Reader is descendant/continuation that doesn't need to mutate active_reader
    unsigned lca_depth = active_reader.lca(reader);
    no_updates = (lca_depth % 4 == 3 && lca_depth >= active_reader.end_idx);
  } while (!seqlock.read_was_safe(seq));

  if (__builtin_expect(no_updates, 1)) {
    return false;
  }

  return does_read_race_slow(reader);
}

bool shadow_label::does_write_race(const os_label &writer) {
  uint32_t seq;
  bool is_same_writer = false;

  do {
    seq = seqlock.begin_read();
    // Fast-path: Same writer with no preceding write race
    is_same_writer = active_reader.is_identical(writer) && (write_depth >= writer.end_idx);
  } while (!seqlock.read_was_safe(seq));

  if (__builtin_expect(is_same_writer, 1)) {
    return false;
  }

  return does_write_race_slow(writer);
}


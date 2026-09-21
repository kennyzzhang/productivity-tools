#include "shadow_label.h"

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

  seqlock.end_write();
  return write_race == parallel || write_race == within;
}

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

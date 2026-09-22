#ifndef _LEB8_SINGLE_H
#define _LEB8_SINGLE_H

#pragma GCC visibility push(default)

#include <cilk/cilkprace_ablation.h>
#include <cilk/os_label.h>
#include <ostream>
#include "atomic_seqlock.h"

#if CILKPRACE_ABL_CACHE_ALIGN
// One shadow entry per cache line, so a shadow_label is never split.
#define CILKPRACE_SHADOW_ALIGN 64
#else
// Natural alignment only: entries straddle cache lines.
#define CILKPRACE_SHADOW_ALIGN 8
#endif

class alignas(CILKPRACE_SHADOW_ALIGN) shadow_label {
public:
  os_label active_reader;
  uint16_t write_depth = 0;
  uint8_t _pad[2] = {0};
  atomic_seqlock seqlock;

  bool does_read_race(const os_label &reader);
  bool does_write_race(const os_label &writer);

  bool does_read_race_slow(const os_label &reader);
  bool does_write_race_slow(const os_label &writer);

#ifdef ENABLE_LABEL_PRINTING
  inline friend std::ostream &operator<<(std::ostream &os,
                                         const shadow_label &l) {
    os << "Active Reader: " << l.active_reader << " write_depth: " << l.write_depth;
    return os;
  }
#endif
};

static_assert(sizeof(os_label) == 56, "os_label must be 56 bytes");
static_assert(sizeof(shadow_label) == 64, "shadow_label must be 64 bytes");
static_assert(alignof(shadow_label) == CILKPRACE_SHADOW_ALIGN,
              "shadow_label must match CILKPRACE_SHADOW_ALIGN");
static_assert(__builtin_offsetof(shadow_label, active_reader) == 0, "active_reader must be at offset 0");
static_assert(__builtin_offsetof(shadow_label, write_depth) == 56, "write_depth must be at offset 56");
static_assert(__builtin_offsetof(shadow_label, seqlock) == 60, "seqlock must be at offset 60");

#pragma GCC visibility pop

#endif /* _LEB8_SINGLE_H */

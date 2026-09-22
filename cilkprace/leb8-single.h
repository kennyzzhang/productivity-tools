#ifndef _LEB8_SINGLE_H
#define _LEB8_SINGLE_H

#pragma GCC visibility push(default)

#include <cilk/cilkprace_ablation.h>
#include <cilk/os_label.h>
#include <ostream>
#include "atomic_seqlock.h"

// A shadow_label is os_label + write_depth + padding + seqlock.
#define CILKPRACE_SHADOW_RAW (CILKPRACE_LABEL_WORDS * 8 + 16)

#if CILKPRACE_ABL_CACHE_ALIGN
// Align to a power of two >= the entry size, capped at a cache line, so an
// entry never straddles a line. At the default width that is 64 (one entry per
// line); at 2 words it is 32, so two entries share a line -- which is the point
// of narrowing the label. Widths whose raw size is not a power of two get
// padded up to the next one and save nothing, so prefer 2 or 6.
#if CILKPRACE_SHADOW_RAW <= 16
#define CILKPRACE_SHADOW_ALIGN 16
#elif CILKPRACE_SHADOW_RAW <= 32
#define CILKPRACE_SHADOW_ALIGN 32
#else
#define CILKPRACE_SHADOW_ALIGN 64
#endif
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

static_assert(sizeof(os_label) == CILKPRACE_LABEL_WORDS * 8 + 8,
              "os_label must be CILKPRACE_LABEL_WORDS words + end_idx + padding");
#if CILKPRACE_ABL_CACHE_ALIGN
static_assert(sizeof(shadow_label) == CILKPRACE_SHADOW_ALIGN,
              "cache-aligned shadow_label is padded up to its alignment");
#else
static_assert(sizeof(shadow_label) == CILKPRACE_SHADOW_RAW,
              "shadow_label must be CILKPRACE_SHADOW_RAW bytes");
#endif
static_assert(alignof(shadow_label) == CILKPRACE_SHADOW_ALIGN,
              "shadow_label must match CILKPRACE_SHADOW_ALIGN");
static_assert(__builtin_offsetof(shadow_label, active_reader) == 0,
              "active_reader must be at offset 0");
static_assert(__builtin_offsetof(shadow_label, write_depth) == sizeof(os_label),
              "write_depth must follow active_reader");
static_assert(__builtin_offsetof(shadow_label, seqlock) == sizeof(os_label) + 4,
              "seqlock must follow write_depth and its padding");

#pragma GCC visibility pop

#endif /* _LEB8_SINGLE_H */

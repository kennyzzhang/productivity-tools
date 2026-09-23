#ifndef _LEB8_SINGLE_H
#define _LEB8_SINGLE_H

#pragma GCC visibility push(default)

#include <cilk/cilkprace_ablation.h>
#include <cilk/os_label.h>
#include <ostream>
#include "atomic_seqlock.h"

// A shadow_label is os_label + write_depth + padding + seqlock.
#define CILKPRACE_SHADOW_RAW (CILKPRACE_LABEL_WORDS * 8 + 16)

// No alignas: the shadow is a page-aligned array indexed by granule, so at the
// default width (64 bytes) every entry already starts on a cache line, and at
// 2 words (32 bytes) two entries share one. Forcing alignment measured no
// difference.
class shadow_label {
public:
  os_label active_reader;
  uint16_t write_depth = 0;
  uint8_t _pad[2] = {0};
  atomic_seqlock seqlock;

  // always_inline: this is the read fast path, and it only pays off inlined.
  // It sits right at the inliner's threshold -- adding the same-strand clause
  // (READ_IDENT_FASTPATH) tipped it over, and every read then called it out of
  // line, making rectmul 1.4x slower. Don't leave that to the heuristic.
  __attribute__((always_inline)) bool does_read_race(const os_label &reader);
  // always_inline for the same reason as does_read_race.
  __attribute__((always_inline)) bool does_write_race(const os_label &writer);

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
static_assert(sizeof(shadow_label) == CILKPRACE_SHADOW_RAW,
              "shadow_label must be CILKPRACE_SHADOW_RAW bytes");
static_assert(__builtin_offsetof(shadow_label, active_reader) == 0,
              "active_reader must be at offset 0");
static_assert(__builtin_offsetof(shadow_label, write_depth) == sizeof(os_label),
              "write_depth must follow active_reader");
static_assert(__builtin_offsetof(shadow_label, seqlock) == sizeof(os_label) + 4,
              "seqlock must follow write_depth and its padding");

#pragma GCC visibility pop

#endif /* _LEB8_SINGLE_H */

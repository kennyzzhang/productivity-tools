#ifndef _LEB8_FLAT_H
#define _LEB8_FLAT_H

// leb8-single's check on 64-byte shadow entries that hold their label inline,
// for leb8-anchor labels. Nothing is allocated per strand and nothing needs
// reclaiming: the shadow grows with the bytes the program touches, not with
// the strands it runs.
//
// An entry holds bits [0, 384) of a label, its end and its write depth, as
// whole-label bit indices. A label longer than that is a fatal error (a
// serial chain of more than about 96 unsynced spawns). With adaptive lines a
// line's entry may instead be refined, one level deep: w[0] points at a block
// of plain child entries of 2^aux bytes each, as fine as the finest access the
// line has seen (a finer one rebuilds the block; see leb8-flat.cpp), and w[1]
// chains the blocks that rebuilds retired. Children have no lock of their
// own: the line's seqlock covers them. The all-zero entry is the empty one,
// like a zeroed leb8-single entry. Updates take the entry's seqlock; reads
// are optimistic and retry if a writer intervened, as in leb8-single.
//
// Build with CILKPRACE_LABEL_IMPL=leb8-flat, which gives the runtime
// leb8-anchor labels (cheetah/runtime/CMakeLists.txt).

#pragma GCC visibility push(default)

#include <cilk/cilkprace_ablation.h>
#include <cilk/os_label.h>
#include <cstdint>
#include "atomic_seqlock.h"

#ifndef _OS_LABEL_LEB8_ANCHOR_H
#error "leb8-flat needs leb8-anchor labels (CILKPRACE_LABEL_IMPL=leb8-flat)"
#endif

namespace leb8_flat {
constexpr unsigned kWords = 6, kBits = kWords * 64;
enum : uint8_t { kPlain = 0, kRefined = 1 };

// An entry's fields, copied whole under the seqlock.
struct fields_t {
  uint64_t w[kWords] = {};
  uint16_t end = 0, write_depth = 0;
  uint8_t mode = kPlain, aux = 0;
};
} // namespace leb8_flat

class shadow_label {
public:
  leb8_flat::fields_t f;
  atomic_seqlock seqlock;

  // always_inline: the fast paths only pay off inlined (see leb8-single.h).
  __attribute__((always_inline)) bool does_read_race(const os_label &reader);
  __attribute__((always_inline)) bool does_write_race(const os_label &writer);
  bool does_read_race_slow(const os_label &reader);
  bool does_write_race_slow(const os_label &writer);

#if CILKPRACE_ADAPTIVE_GRANULE
  // As in leb8-short-ptr.h.
  unsigned does_read_race_part(const os_label &reader, unsigned lo, unsigned hi);
  unsigned does_write_race_part(const os_label &writer, unsigned lo, unsigned hi);
  unsigned does_read_race_line(const os_label &reader, unsigned off, unsigned n);
  unsigned does_write_race_line(const os_label &writer, unsigned off, unsigned n);
  bool does_read_race_block(const os_label &reader);
  bool does_write_race_block(const os_label &writer);
#endif
};

static_assert(sizeof(shadow_label) == 64, "a flat entry is 64 bytes");

#if CILKPRACE_ADAPTIVE_GRANULE
// Empties bytes [beg, beg + n), whose first line is s, recycling blocks.
void leb8_flat_clear(shadow_label *s, uintptr_t beg, size_t n);
// Merges refined entry s back into one if its children agree.
void leb8_flat_merge(shadow_label &s);
inline bool leb8_flat_refined(const shadow_label &s) { return s.f.mode == leb8_flat::kRefined; }
#endif

#pragma GCC visibility pop

#endif /* _LEB8_FLAT_H */

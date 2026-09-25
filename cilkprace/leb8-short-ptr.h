#ifndef _LEB8_SHORT_PTR_H
#define _LEB8_SHORT_PTR_H

// leb8-single's check on one-word shadow entries. Each entry points at an
// immutable copy of the active reader's leb8-single label, with end_idx and
// write_depth packed into the bits the pointer does not use:
//
//   bits  0-40  record address >> 7 (records are 128-byte aligned, below 2^48)
//   bits 41-51  active reader's end_idx: the label's own, or shorter after
//               widening to a P node
//   bits 52-63  write_depth
//
// An update is one CAS of the entry, and 0 is the empty entry. A strand copies
// its label into a record the first time it stores it, and keeps the pointer
// in the tool word after its label, which the runtime zeroes whenever the
// label changes (see tool_word in cheetah's pedigree-internal.h). Records are
// never freed.
//
// Build with CILKPRACE_LABEL_IMPL=leb8-short-ptr.

#pragma GCC visibility push(default)

#include <cilk/os_label.h>
#include <cstdint>

#ifndef _OS_LABEL_LEB8_SINGLE_H
#error "leb8-short-ptr uses leb8-single labels (USE_OS_LABEL_LEB8_SINGLE)"
#endif

static_assert(CILKPRACE_LABEL_WORDS * 64 < 2048, "end_idx must fit in 11 bits");

// Copies l into a new record. Out of line: once per strand.
const os_label *leb8_short_ptr_store(const os_label &l);

class shadow_label {
public:
  uint64_t entry = 0;

  // always_inline: CSI links the tool as available_externally bitcode, and
  // these are the hot paths (see leb8-single.h).
  __attribute__((always_inline)) bool does_read_race(const os_label &reader);
  __attribute__((always_inline)) bool does_write_race(const os_label &writer);

  // The updates, out of line. w is the entry as the inline part read it.
  bool does_read_race_slow(const os_label &reader, uint64_t w, unsigned lca_depth);
  bool does_write_race_slow(const os_label &writer, uint64_t w);
};

static_assert(sizeof(shadow_label) == 8, "a shadow entry is one word");

#pragma GCC visibility pop

#endif /* _LEB8_SHORT_PTR_H */

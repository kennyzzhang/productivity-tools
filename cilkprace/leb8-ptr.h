#ifndef _LEB8_PTR_H
#define _LEB8_PTR_H

// leb8-single on a pointer-table shadow. The check is leb8-single's
// (leb8-single.cpp); what changes is where the reader's label lives. Instead of
// a 64-byte shadow_label per granule holding a copy of it, each granule holds
// one 64-bit word
//
//   bits  0-31  id of the active reader's label in a global label table
//   bits 32-46  active reader's end_idx: the label's own, or shorter after
//               widening to a P node (the table entry is never changed)
//   bit  47     the label has spilled out of its window (kLeb8PtrDeep)
//   bits 48-63  write_depth
//
// so the shadow is 8x smaller, a check is one load and, when the entry
// changes, one 64-bit CAS in place of the seqlock and lock (the locked section
// in leb8-single is a pure read-modify-write of the entry), and the
// same-strand tests compare ids, not label words. The all-zero word is the
// empty entry: table entry 0 is the all-zero label, exactly like a zeroed
// shadow_label.
//
// The labels are leb8-anchor's (os_label_leb8_anchor.h): leb8-single's with no
// depth limit, the newest bits in a window starting at label.base and older
// ones in chunks. end_idx and write_depth are stored relative to the label's
// base, which is 0 for any label that never left its window, and fit whenever
// base <= write_depth. When write_depth falls below base (a deep label
// compared with one that diverged above its window) the entry becomes wide
// instead: its end field reads kLeb8PtrWide, and the id names a record of its
// own holding the label with the whole-label end_idx and write_depth. The deep
// bit, set in wide entries too, lets the inline checks send every entry that
// is not a plain shallow one out of line without reading its table record.
//
// A strand's label is added to the table the first time the strand stores it
// in an entry, and the id is kept in the tool word after the strand's label in
// its pedigree frame, which the runtime resets whenever the label changes (see
// tool_word in cheetah's pedigree-internal.h), keeping only a bit that says
// whether the label has left its window. The table only grows.
//
// Build with CILKPRACE_LABEL_IMPL=leb8-ptr.

#pragma GCC visibility push(default)

#include <cilk/cilkprace_ablation.h>
#include <cilk/os_label.h>
#include <cstdint>

#ifndef _OS_LABEL_LEB8_ANCHOR_H
#error "leb8-ptr needs the runtime's labels to be leb8-anchor (USE_OS_LABEL_LEB8_ANCHOR)"
#endif

struct alignas(64) leb8_ptr_record {
  os_label label;
  // A wide entry's whole-label end_idx and write_depth; unused otherwise.
  uint32_t end, write_depth;
};

// Bits of the 16-bit end field: the deep flag, and the value of a wide entry's
// field, which has it set. Real ends are 0 or 3 mod 4, and a window is shorter
// than kLeb8PtrDeep.
constexpr unsigned kLeb8PtrDeep = 0x8000;
constexpr unsigned kLeb8PtrWide = 0xFFFE;
static_assert(os_label::window_bits < kLeb8PtrDeep, "relative ends must fit below kLeb8PtrDeep");

extern leb8_ptr_record *leb8_ptr_table;

// Adds l to the table and returns its id. Out of line: once per strand.
uint32_t leb8_ptr_store_label(const os_label &l);

// Reserves the table.
void leb8_ptr_init();

class shadow_label {
public:
  uint64_t entry = 0;

  // always_inline: CSI links the tool as available_externally bitcode, and
  // these are the hot paths (see leb8-single.h).
  __attribute__((always_inline)) bool does_read_race(const os_label &reader);
  __attribute__((always_inline)) bool does_write_race(const os_label &writer);

  // The updates, out of line like leb8-single's locked paths. w is the entry
  // as the inline part read it, and lca_depth its LCA with the reader, which
  // holds only if both labels are within their windows.
  bool does_read_race_slow(const os_label &reader, uint32_t cur_id, uint64_t w,
                           unsigned lca_depth);
  bool does_write_race_slow(const os_label &writer, uint32_t cur_id, uint64_t w);
};

static_assert(sizeof(shadow_label) == 8, "a shadow entry is one word");

#pragma GCC visibility pop

#endif /* _LEB8_PTR_H */

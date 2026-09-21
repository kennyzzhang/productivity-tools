#ifndef _LEB8_SINGLE_H
#define _LEB8_SINGLE_H

#pragma GCC visibility push(default)

#include <cilk/os_label.h>
#include <ostream>
#include "atomic_seqlock.h"

#pragma pack(push, 2)
class alignas(64) shadow_label {
public:
  os_label active_reader;
  uint16_t write_depth = 0;
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
#pragma pack(pop)

static_assert(sizeof(os_label) == 58, "os_label must be 58 bytes");
static_assert(sizeof(shadow_label) == 64, "shadow_label must be 64 bytes");

#pragma GCC visibility pop

#endif /* _LEB8_SINGLE_H */

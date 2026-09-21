#ifndef _LEB8_RANGE_H
#define _LEB8_RANGE_H

#pragma GCC visibility push(default)

#include <cilk/os_label.h>
#include <ostream>
#include "atomic_seqlock.h"

struct alignas(64) shadow_label {
    // Cache line 0 (64 bytes): Read race fast path
    os_label last_reader_range;  // 56 bytes
    atomic_seqlock seqlock;      // 4 bytes
    bool is_range = false;       // 1 byte
    uint8_t _pad0[3] = {0};      // 3 bytes

    // Cache line 1 (64 bytes): Write path
    os_label last_writer;        // 56 bytes
    uint8_t _pad1[8] = {0};      // 8 bytes

public:
    bool does_read_race(const os_label &reader);
    bool does_write_race(const os_label &writer);
    bool does_read_race_slow(const os_label &reader);
    bool does_write_race_slow(const os_label &writer);

#ifdef ENABLE_LABEL_PRINTING
    inline friend std::ostream &operator<<(std::ostream &os,
                                           const shadow_label &l) {
      os << "Last Writer: " << l.last_writer << std::endl;
      os << (l.is_range ? "Range" : "Point") << " Reader: " << l.last_reader_range
         << std::endl;
      return os;
    }
#endif
};

#pragma GCC visibility pop

#endif /* _LEB8_RANGE_H */

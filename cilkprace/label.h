#include "codes.h"

class os_label
{
  bitset labels = {0};
  uint64_t offset = 0;

  void append(const uint64_t& label)
  {
    uint64_t encoded = encode_to_nibbles(label);
    offset += append_right_nibbles_to_bitset(encoded, labels, offset);
  }

  bool operator||(const os_label&& rhs) const
  {
    bitset scratch;
    lca_nibble(labels, rhs, scratch);
    //FIXME
    return true;
  }
  
  bitset operator&&(const os_label&& rhs) const
  {
    bitset scratch;
    lca_nibble(labels, rhs, scratch);
    return scratch;
};


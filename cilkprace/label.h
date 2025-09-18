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

  // Returns true if in parallel
  bool operator||(const os_label&& rhs) const
  {
    bitset scratch;

    // 4 cases:
    // 1. No LCA-- series
    // 2. LCA is one of them-- parent child, series
    // 3. LCA, both have longer labels. series or parallel depends on parity
    // 4. Identical labels

    int matching_nibbles = lca_nibble(labels, rhs.labels, scratch);
    // 1. No LCA
    if (matching_nibbles == 0)
      return false;
    // 4. Identical Labels
    else if (matching_nibbles == -1)
      return true;
    else
    {
      // To determine this, we have to read the next label
      uint64_t left = extract_nibbles_from_bitset(labels, matching_nibbles);
      uint64_t right = extract_nibbles_from_bitset(rhs.labels, matching_nibbles);
      // 2. Parent-Child relationship 
      if (left == 0 || right == 0)
        return true;
      // Convert to integer
      left = decode_from_nibbles(left);
      right = decode_from_nibbles(right);
      // 3. Parity check
      if (left % 2 == right % 2)
        return true;
      return false;
    }
  }
  
};


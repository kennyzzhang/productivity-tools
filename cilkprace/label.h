//#include "codes.h"

#include "lca.h"

class os_label
{
  bitset labels = {0};
  uint64_t offset = 0;

  void append(const uint64_t& label)
  {
    labels[label] = offset++;
  }

  // Returns true if in parallel
  bool operator||(const os_label&& rhs) const
  {
    // 4 cases:
    // 1. No LCA-- series
    // 2. LCA is one of them-- parent child, series
    // 3. LCA, both have longer labels. series or parallel depends on parity
    // 4. Identical labels

    size_t num_matches = count_matching(labels, rhs.labels);
    // 1. No LCA
    if (num_matches == 0)
      return false;
    // 4. Identical Labels
    else if (num_matches == -1)
      return true;
    else
    {
      // To determine this, we have to read the next label
      uint8_t left = labels[num_matches];
      uint8_t right = rhs.labels[num_matches];
      
      // 2. Parent-Child relationship 
      if (left == 0 || right == 0)
        return true;
      
      // 3. Parity check
      if (left % 2 == right % 2)
        return true;
      return false;
    }
  }
  
};


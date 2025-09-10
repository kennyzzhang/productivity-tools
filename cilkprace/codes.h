#include <cstddef>
#include <cstdint>
#include <cassert>
#include <immintrin.h>

constexpr size_t code_max_length = 512;
constexpr size_t code_nbytes = code_max_length/64;

using bitset = uint64_t[code_nbytes];

static constexpr uint64_t nibble_spread = 0x7777777777777777ull;
static constexpr uint64_t nibble_metadata = ~nibble_spread;


static uint64_t encode_to_nibbles(uint64_t num)
{
  num++;
  // Spread the numbers into nibbles by the mask
  uint64_t spread = _pdep_u64(num, nibble_spread);

  // Calculate the number of nibbles used to represent this number
  int nibbles_needed =  (64 - __builtin_clzll(num)+2)/3;
  // Create a mask for the continuation bits.
  // 1 represents more data. 0 represents END
  // So all nibbles but the LSN need a 1. Create the mask, and unset the leftmost
  uint64_t cont_bits = ((1 << nibbles_needed) - 1) ^ 1;
  uint64_t metadata = _pdep_u64(cont_bits, nibble_metadata);

  assert((spread & metadata) == 0);
  return spread | metadata;

}

static uint64_t count_nibbles(uint64_t buffer)
{
  // Gather the continues and flip them for count leading zero. shift away extras
  // That is, 0 represents a continue
  uint64_t metadata = (~_pext_u64(buffer, nibble_metadata)) << 48;

  // Count the number of nibbles
  uint64_t length_nibbles = 1 + __builtin_clzll(metadata);
  assert (length_nibbles <= 16);
  return length_nibbles;
}

static uint64_t extract_data(uint64_t buffer, uint64_t len_bits)
{
    // Extract raw data to read
    uint64_t raw_data = _pext_u64(buffer, nibble_spread);
    
    // Shift away the remaining data
    raw_data >>= 48 - len_bits;
    return raw_data;
}

static uint64_t decode_from_nibbles(uint64_t buffer)
{
  // Left shift the buffer such that the leftmost nibble is readable
  buffer <<= (__builtin_clzll(buffer)/4) * 4;

  uint64_t length_nibbles = count_nibbles(buffer);

  uint64_t length_bits = length_nibbles *3;
  uint64_t data = extract_data(buffer, length_bits);
  // Data is currently stored in the rightmost 48 bits
  
  return data - 1;
}

static bool less_nibble(uint64_t n1, uint64_t n2)
{
    n1 <<= __builtin_clzll(n1);
    n2 <<= __builtin_clzll(n2);

    return n1 < n2;
}

static uint64_t lca_nibble(uint64_t n1, uint64_t n2)
{
    n1 <<= __builtin_clzll(n1);
    n2 <<= __builtin_clzll(n2);

    uint64_t diff = n1 ^ n2;
    if (diff == 0) return n1;

    int num_left_matching_nibbles = __builtin_clzll(diff) / 4;
    int nibbles = count_nibbles(n1);

    // TODO: Mask off instead of all this shifting?

    if (nibbles == num_left_matching_nibbles) 
        return n1 >> (64 - nibbles*4);
        return lca_nibble(n1 << (nibbles * 4), n2 << (nibbles *4));

}

#include <iostream>
#include <bitset>

int main()
{
    srand(time(NULL));
    uint64_t num1 = rand();
    uint64_t num2 = rand();
    uint64_t encoded1 = encode_to_nibbles(num1);
    uint64_t encoded2 = encode_to_nibbles(num2);
    uint64_t decoded1 = decode_from_nibbles(encoded1);
    uint64_t decoded2 = decode_from_nibbles(encoded2);

    std::cout << num1 << " < " << num2 << "? " << (num1 < num2 ? "YES" : "NO") << std::endl;
    std::cout << "ENCODED 1: " << std::bitset<8*sizeof(encoded1)>(encoded1) << std::endl;
    std::cout << "ENCODED 2: " << std::bitset<8*sizeof(encoded2)>(encoded2) << std::endl;
    
    std::cout << "LCA: " << lca_nibble(encoded1, encoded2) << std::endl;

    std::cout << "DECODED 1: " << std::bitset<8*sizeof(encoded1)>(decoded1) << std::endl;
    std::cout << "DECODED 2: " << std::bitset<8*sizeof(encoded2)>(decoded2) << std::endl;
    std::cout << num1 << " < " << num2 << "? " << (less_nibble(encoded1, encoded2) ? "YES" : "NO") << std::endl;
}

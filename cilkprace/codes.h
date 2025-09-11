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

static uint64_t pack_nibbles(uint64_t n1, uint64_t n2)
{
    int nibbles_needed =  (64 - __builtin_clzll(n2)+3)/4;
    return (n1 << (nibbles_needed * 4)) | n2;
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
  uint64_t length_nibbles = count_nibbles(buffer);

  uint64_t length_bits = length_nibbles *3;
  uint64_t data = extract_data(buffer, length_bits);
  // Data is currently stored in the rightmost 48 bits
  
  return data - 1;
}

static uint64_t lca_nibble(uint64_t n1, uint64_t n2)
{
    uint64_t diff = n1 ^ n2;
    if (diff == 0) return n1;

    int num_left_matching_nibbles = __builtin_clzll(diff) / 4;
    int num_right_different_nibbles = 16 - num_left_matching_nibbles;

    uint64_t bit = 1ull << (num_right_different_nibbles*4 +3);
    // We need to trim the match to the end of the last nibble
    // That is, while we're in a nibble, back up
    while(bit & n1)
    {
        num_right_different_nibbles++;
        bit <<= 4;
    }
    uint64_t matching_mask = ~((1ull << (num_right_different_nibbles*4))-1);
    return matching_mask & n1;
    
/*
    uint64_t matching_mask = ~((1ull << (num_right_different_nibbles*4))-1);
    // Mask away rest of number
    uint64_t matching_number = n1 & matching_mask;
    // & away data
    uint64_t matching_metadata = _pext_u64(matching_number, nibble_metadata);
    uint64_t run_of_ones = matching_metadata >> __builtin_ctzll(matching_metadata);
    uint64_t bit_extractor = run_of_ones >> __builtin_ctzll(~run_of_ones);
    return __builtin_clzll(bit_extractor)-48;*/
}

#include <iostream>
#include <bitset>

int main()
{
    
    srand(time(NULL));
    uint64_t num1 = rand() % 10000;
    uint64_t num2 = rand() % 10000;
    uint64_t num3 = rand() % 10000; 
    uint64_t num4 = rand() % 100; 
    uint64_t num5 = rand() % 1000;

    uint64_t encoded1 = encode_to_nibbles(num1);
    uint64_t encoded2 = encode_to_nibbles(num2);
    uint64_t prefix = encode_to_nibbles(num3);
    prefix = pack_nibbles(prefix, encode_to_nibbles(num4));
    prefix = pack_nibbles(prefix, encode_to_nibbles(num5));

    encoded1 = pack_nibbles(prefix, encoded1);
    encoded2 = pack_nibbles(prefix, encoded2);

    // Left shift the buffer such that the leftmost nibble is readable
    encoded1 <<= (__builtin_clzll(encoded1)/4) * 4;
    encoded2 <<= (__builtin_clzll(encoded2)/4) * 4;
    prefix <<= (__builtin_clzll(prefix)/4) * 4;

    uint64_t decoded1 = decode_from_nibbles(encoded1);
    uint64_t decoded2 = decode_from_nibbles(encoded2);

    std::cout << "PREFIX   : " << num3 << std::endl;
    std::cout << "ENCODED 1: " << std::bitset<8*sizeof(encoded1)>(encoded1) << std::endl;
    std::cout << "ENCODED 2: " << std::bitset<8*sizeof(encoded2)>(encoded2) << std::endl;

    std::cout << std::endl;

    std::cout << "PREFIX   : " << std::bitset<8*sizeof(prefix)>(prefix) << std::endl;
    std::cout << "LCA      : " << std::bitset<8*sizeof(encoded2)>(lca_nibble(encoded1, encoded2)) << std::endl;
    std::cout << "LCA      : " << lca_nibble(encoded1, encoded2) << std::endl;

    std::cout << std::endl;

    std::cout << "DECODED 1: " << std::bitset<8*sizeof(encoded1)>(decoded1) << std::endl;
    std::cout << "DECODED 2: " << std::bitset<8*sizeof(encoded2)>(decoded2) << std::endl;
}

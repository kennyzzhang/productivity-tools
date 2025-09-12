#include <stdint.h>

#include <bitset>
#include <iostream>

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <immintrin.h>

constexpr size_t code_max_length = 512;
constexpr size_t code_nbytes = code_max_length/8;

using bitset = uint8_t[code_nbytes];

static constexpr uint64_t nibble_spread = 0x7777777777777777ull;
static constexpr uint64_t nibble_metadata = ~nibble_spread;

static uint64_t extract_nibbles_from_bitset(const bitset& bits, size_t left_offset)
{
    // overflow protection
    size_t overflow_shift = 0;
    if (left_offset*4 >= code_max_length)
        return 0;
    if (left_offset*4 + 64 >= code_max_length)
    {
        overflow_shift = left_offset*4 + 64 - code_max_length;
        left_offset = (code_max_length - 64) / 4;
    }
    // Since we're indexing nibbles, 2 nibbles = 1 byte of index
    // and an odd number of nibbles requires a bitshift by 4
    // before a cast down
    size_t shift = (left_offset % 2) * 4;
    left_offset /=2;

    uint64_t data = *(uint64_t*)&bits[code_nbytes - 8 - left_offset];
    uint64_t next_data = *(uint64_t*)&bits[code_nbytes - 8 - left_offset -1];
    return ((data << shift) | (next_data >> (64 - shift))) << overflow_shift;
}

static size_t append_right_nibbles_to_bitset(uint64_t what, bitset& bits, size_t left_offset)
{
    // Left-align the nibbles
    size_t unused_left_nibbles = __builtin_clzll(what)/4;
    size_t len_nibbles = 16 - unused_left_nibbles;

    what <<= unused_left_nibbles*4;

    // overflow protection
    assert (left_offset*4 < code_max_length);
    assert (left_offset*4 + len_nibbles*4 <= code_max_length);

    // Since we're indexing nibbles, 2 nibbles = 1 byte of index
    // and an odd number of nibbles requires a bitshift by 4
    // before a cast down
    size_t shift = (left_offset % 2) * 4;
    left_offset /=2;

    uint64_t* data = (uint64_t*)&bits[(code_nbytes - 8) - left_offset];
    uint64_t* next_data = (uint64_t*)&bits[code_nbytes - 8 - left_offset -1];
    *data |= what >> shift;
    if (shift)
        *next_data |= (what % 16) << 4;
    return len_nibbles;
}

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

static void lca_nibble(const bitset& _b1, const bitset& _b2, bitset& _into)
{
    const uint64_t* b1 = (const uint64_t*)_b1;
    const uint64_t* b2 = (const uint64_t*)_b2;
    uint64_t* into = (uint64_t*)_into;

    int num_left_matching_nibbles = 0;
    for (int i = code_nbytes/8-1; i >=0; i--)
    {
        into[i] = b1[i] ^ b2[i];
        if (into[i] == 0)
            num_left_matching_nibbles += 16;
        else
        {
            num_left_matching_nibbles += __builtin_clzll(into[i])/4;
            break;
        }
    }
    // Handle exact match
    if (num_left_matching_nibbles == code_nbytes * 2)
    {
        memcpy(_into, _b1, code_nbytes);
        return;
    }
     
    // We know there's a difference. 
    // We have to back up until we find the end of the previous integer
    while(true)
    {
        int right_matching_nibbles = (code_nbytes*2) - num_left_matching_nibbles;
        int word_index = right_matching_nibbles / 16;
        int nibble_index = right_matching_nibbles % 16;
        uint64_t bit = 1ull << (nibble_index*4 + 3);

        if (bit & b1[word_index])
            num_left_matching_nibbles--;
        else
          break;
    }
    
    int matching_bytes = num_left_matching_nibbles / 2;
    memset(_into, 0, code_nbytes);
    memcpy(_into + code_nbytes - matching_bytes, _b1 + code_nbytes - matching_bytes, matching_bytes);
    // And handle a stray nibble 
    if (num_left_matching_nibbles % 2)
      _into[code_nbytes - matching_bytes -1] |= _b1[code_nbytes - matching_bytes -1] & 0xF0;
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
#include <iomanip>

static void cout_bitset(bitset& bs)
{
    for (int i = code_nbytes-1; i >= 0; i--)
        std::cout << std::setfill('0') << std::setw(2) << std::hex << (uint16_t)bs[i];
    std::cout << std::endl;
}

int main()
{
    srand(time(NULL));
    uint64_t num1 = rand() % 1000;
    uint64_t num2 = rand() % 1000;
    uint64_t num3 = rand() % 1000; 
    uint64_t num4 = rand() % 100; 
    uint64_t num5 = rand() % 10;

    uint64_t encoded1 = encode_to_nibbles(num1);
    uint64_t encoded2 = encode_to_nibbles(num2);
    uint64_t prefix = encode_to_nibbles(num3);
    prefix = pack_nibbles(prefix, encode_to_nibbles(num4));
    prefix = pack_nibbles(prefix, encode_to_nibbles(num5));

    encoded1 = pack_nibbles(prefix, encoded1);
    encoded2 = pack_nibbles(prefix, encoded2);

    bitset b1 = {0};
    bitset b2 = {0};

    size_t offset = 0;

//    encoded1 = (0xfa0980909b93ull);
//    encoded2 = (0xfa098090c6ull);

    offset = append_right_nibbles_to_bitset(encoded1, b1, 0);
    offset += append_right_nibbles_to_bitset(encoded2, b1, offset);
    
    offset = append_right_nibbles_to_bitset(encoded1, b2, 0);
    offset += append_right_nibbles_to_bitset(encoded1, b2, offset);


//
//
    //FIXME
    std::cout << "ENCODED 1/2" << std::endl;
    cout_bitset(b1);
    std::cout << "ENCODED 1/1" << std::endl;
    cout_bitset(b2);

    std::cout << "LCA" << std::endl;
    bitset b3 = {0};
    lca_nibble(b1, b2, b3);
    cout_bitset(b3);

    std::cout << "uint64_t" << std::endl << std::endl << std::endl;

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

    std::cout << std::endl;

    bitset bs;
    for (int i = 0; i < code_nbytes; i++)
        bs[i] = i % 256;
    cout_bitset(bs);
    std::cout << std::endl;
    std::cout << "EXTRACT : " << std::hex << extract_nibbles_from_bitset(bs, 124) << std::endl;

}


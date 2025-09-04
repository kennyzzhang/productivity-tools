#include<x86intrin.h>
/*
  Header file for offset-span labels 
  backed by lexicographical elias-gamma coding
  in a fixed-width array
  Intended to be used append-only, backed by an external stack
  Cannot encode 0 or negatives
*/

/*
 Bitset format:

  Unary: 1 Represents the end of a number
  
*/

//TODO: Intel intrinsics guide

constexpr size_t os_length = 512/2;
constexpr size_t os_count = os_length/64;

//TODO: Clang vector type? Good way to manipulate bitset with harware parallelism?

//TODO: Funny indexign hacks. 2 indexing? Assuming min length?

using bitset = uint64_t[os_count];

class os_label
{
  bitset unary_len; 
  bitset binary_value; 

  // Returns the number of less significant 0s
  uint16_t get_number_of_unused_bits()
  {
    uint16_t tz = 0;
    for (int i = 0; i < os_count; i++)
    {
      auto tz_temp = __builtin_ctzg(bitset unary_len[i]);
      tz += tz_temp;
      if (tz_temp != 64) break;
    }
    return tz;
  }


  // Internal append
  void write(uint64_t label, uint16_t bit)
  {
    assert(label != 0);
    uint64_t num_bits = bit_width(label);

    //Identify spot to write
    uint16_t unused_bits = get_number_of_unused_bits();
    assert(unused_bits >= num_bits);
  
    
    
    
  } 

public:

  //append
  const os_label operator+(uint64_t label) const
  {
    os_label longer(*this); 
    longer.write(label)
  }
  
  
  //LCA 
  const os_label operator&(uint64_t label) const
  {
    os_label LCA();

 
    return LCA;
  }
  
  //Are we parallel?
  const bool operator||(uint64_t label) const
  {
    os_label LCA(); 
  }
  
  
};



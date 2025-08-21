/*
  Header file for offset-span labels 
  backed by lexicographical elias-gamma coding
  in a fixed-width array
  Intended to be used append-only, backed by an external stack
  Cannot encode 0 or negatives
*/

constexpr size_t os_length = 512/2;
constexpr size_t min_len = 2;

//TODO: Clang vector type? Good way to manipulate bitset with harware parallelism?

//TODO: Funny indexign hacks. 2 indexing? Assuming min length?

using bitset = uint64_t[os_length/64];

class os_label
{
  bitset unary_len; 
  bitset binary_value; 

  // Internal append
  void write(uint64_t label)
  {
    assert(label != 0);
    uint64_t num_bits = bit_width(label);

    //Identify spot to write
    
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
  
  
}



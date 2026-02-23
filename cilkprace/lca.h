#include <stdint.h>

#include <bitset>
#include <iostream>

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstring>

constexpr size_t code_max_length = 512;
constexpr size_t code_nbytes = code_max_length/8;

using bitset = uint8_t[code_nbytes];

static size_t count_matching(const bitset &a, const bitset &b)
{
    for (size_t i = 0; i < code_nbytes; i++)
    {
        if (a[i] != b[i])
            return i;
    }
    return -1;
}

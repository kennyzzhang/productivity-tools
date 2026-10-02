#include <cstdint>
#include <array>
#include <limits>
#include <list>
#include <ostream>
#include <vector>

class piston_t {
  using idx_t = uint64_t;
  using val_t = int32_t;
  using block_bits_t = uint64_t;

  static constexpr val_t INFTY = std::numeric_limits<val_t>::max();
  static constexpr size_t block_bits = 8 * sizeof(block_bits_t);

  struct leftblock_t {
    idx_t blockidx;
    block_bits_t smqbitarray;
    val_t endval;
    val_t blockmin;
    unsigned refcnt; // TODO: implement garbage collection

//    leftblock_t() : blockidx(0), smqbitarray(0), endval(0),
//      blockmin(INFTY), refcnt(0) {}

    val_t smq(unsigned subidx) const {
      return endval - __builtin_popcountll(smqbitarray << subidx);
    }
  };

public:
  class iterator {
    uintptr_t blockptr : 56;
    unsigned subidx : 6;

    template<typename T1, typename T2>
    static T1 force_cast(T2 x) {
      T1 ret;
      *reinterpret_cast<T2*>(&ret) = x;
     return ret;
    }

    iterator(std::list<leftblock_t>::iterator block_it, unsigned subidx)
      : blockptr(force_cast<uintptr_t>(block_it)), subidx(subidx) {}

    std::list<leftblock_t>::iterator get_block_it() const {
      return force_cast<std::list<leftblock_t>::iterator>(blockptr);
    }

    unsigned get_subidx() const {
      return subidx;
    }

    friend class piston_t;
  };
private:

  unsigned lastblock_subidx;
  std::list<leftblock_t> leftblocks;
  std::vector<std::list<leftblock_t>::iterator> to_update;
  std::array<val_t, 65> rightblockmins;

  // Invariants:
  // assert(std::advance(leftblocks.begin(), i)->blockidx == i); // will change when garbage collection is implemented
  // assert(std::prev(leftblocks.end()).blockidx - to_update[i].blockidx) == 1 << i); // will change when garbage collection is implemented
  // assert(rightblockmin(64) == INFTY);

  val_t& rightblockmin(int idx) {
    return const_cast<val_t&>(rightblockmin_const(idx));
  }

  const val_t& rightblockmin_const(int idx) const {
    return rightblockmins[idx + 1];
  }

  // blockidx_diff == 1 does happen, so it is important to handle clz(0).
  // blockidx_diff == 0 will wrap around to idx 63. This is fine, so long as we
  // keep rightblockmin(63) == rightblockmins[64] == INFTY
  val_t rightblockmin_from_diff(idx_t blockidx_diff) const {
    blockidx_diff--;
    int idx = 63 - (blockidx_diff ? __builtin_clzll(blockidx_diff) : 64);
    return rightblockmin_const(idx);
  }

  void extend_leftblocks() {
    lastblock_subidx = 0;
    // TODO: inherit this value of lastblock from append_inc/dec?
    const auto& lastblock = *std::prev(leftblocks.end());
    idx_t new_blockidx = lastblock.blockidx + 1;
    leftblocks.push_back((struct leftblock_t){
      .blockidx = new_blockidx,
      .smqbitarray = 0,
      .endval = lastblock.endval,
      .blockmin = INFTY,
      });

    val_t prevrightblockmin = lastblock.smq(0);
    for (int i = 0; i < to_update.size(); i++) {
      // TODO: if handling garbage collection, do more complex update
      prevrightblockmin = to_update[i]->blockmin = rightblockmin(i) =
        std::min(prevrightblockmin, to_update[i]->blockmin);
      to_update[i]++;
    }

    if ((new_blockidx & new_blockidx - 1) == 0) {
      to_update.push_back(leftblocks.begin());
    }
  }

public:
  piston_t() : lastblock_subidx(0), leftblocks({{
      .blockidx = 0,
      .smqbitarray = 0,
      .endval = 0,
      .blockmin = INFTY,
      }}), to_update() {
    std::fill(rightblockmins.begin(), rightblockmins.end(), INFTY);
  }

  void append_inc() {
    // TODO: consider if we should maintain a cached value of lastblock?
    auto& lastblock = *std::prev(leftblocks.end());
    lastblock.smqbitarray |= 1ull << (block_bits - 1 - lastblock_subidx);
    lastblock.endval++;
    if (++lastblock_subidx >= block_bits) {
      extend_leftblocks();
    }
  }

  void append_dec() {
    // TODO: consider if we should maintain a cached value of lastblock?
    auto& lastblock = *std::prev(leftblocks.end());
    lastblock.smqbitarray &= lastblock.smqbitarray - 1;
    lastblock.endval--;
    if (++lastblock_subidx < block_bits) {
      extend_leftblocks();
    }
  }

  iterator end() {
    return {std::prev(leftblocks.end()), lastblock_subidx};
  }


  __attribute__((noinline)) val_t rmq(iterator beg) const;

  void erase(iterator it) {
    //TODO: garbage collection
  }

  friend std::ostream& operator<<(std::ostream& os, const piston_t& x);
  friend std::ostream& operator<<(std::ostream& os, const piston_t::leftblock_t& x);
};

//#pragma clang attribute push (__attribute__((target("sse4"))), apply_to=function)
// Uses popcnt. ~2.9s
//#pragma clang attribute push (__attribute__((target("sse4,lzcnt"))), apply_to=function)
// Uses lzcnt, popcnt. ~3.9s
// Baseline
// Baseline: ~5.4s
//#pragma clang attribute push (__attribute__((target("lzcnt"))), apply_to=function)
// Uses lzcnt. ~5.4s
//#pragma clang attribute push (__attribute__((target("bmi2,sse4"))), apply_to=function)
// Uses bzhi/shlx, popcnt. ~6.2s
//#pragma clang attribute push (__attribute__((target("bmi2,sse4,lzcnt"))), apply_to=function)
// Uses lzcnt, popcnt, bzhi/shlx. ~6.7s
//#pragma clang attribute push (__attribute__((target("bmi2,lzcnt"))), apply_to=function)
// Uses lzcnt, bzhi/shlx. ~7.8s
//#pragma clang attribute push (__attribute__((target("bmi2"))), apply_to=function)
// Uses bzhi/shlx. ~8.9s

__attribute__((noinline)) piston_t::val_t piston_t::rmq(iterator beg) const {
  const leftblock_t& leftblock = *beg.get_block_it();
  unsigned subidx = beg.get_subidx();

  val_t ret = INFTY;

  ret = std::min(ret, leftblock.smq(subidx));

  ret = std::min(ret, leftblock.blockmin);

  // TODO: consider if we should maintain a cached value of lastblock?
  auto& lastblock = *std::prev(leftblocks.end());

  idx_t blockidx_diff = lastblock.blockidx - leftblock.blockidx;
  // TODO: branchless? Can just min with lastblock.smq(63) for noop
  if (blockidx_diff) {
    ret = std::min(ret, lastblock.smq(0));
  }

  ret = std::min(ret, rightblockmin_from_diff(blockidx_diff));

  return ret;
}

std::ostream& operator<<(std::ostream& os, const piston_t::leftblock_t& x) {
  os << x.blockidx << ":" << std::hex << x.smqbitarray << ", " << x.endval << ", " << x.blockmin;
  return os;
}

std::ostream& operator<<(std::ostream& os, const piston_t& x) {
  os << x.lastblock_subidx << std::endl;
  for (auto& leftblock : x.leftblocks) {
    os << leftblock << std::endl;
  }
  os << std::endl;
//  std::vector<std::list<leftblock_t>::iterator> to_update;
  for (auto val : x.rightblockmins) {
    os << val << ", ";
  }
  os << std::endl;
  return os;
}

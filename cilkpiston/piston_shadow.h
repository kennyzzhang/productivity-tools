// -*- C++ -*-
#ifndef __PISTON_SHADOW_H__
#define __PISTON_SHADOW_H__

#include "piston.h"
#include "race_info.h"
#include <cstdint>
#include <unordered_map>

// A previous access to a byte: where it happened in the SP-tree walk, and
// where it happened in the program, for race reports.
struct PistonAccess_t {
  piston_t::iterator pos;
  AccessLoc_t loc;

  bool valid() const { return pos.valid(); }

  void set(piston_t::iterator p, const AccessLoc_t &l) {
    pos = p;
    loc = l;
  }

  void clear() {
    if (!valid())
      return;
    pos = piston_t::iterator();
    loc.invalidate();
  }
};

// Shadow of one byte of user memory: its last reader and last writer, as in
// SP-bags (Feng & Leiserson '97).
struct PistonShadowEntry_t {
  PistonAccess_t reader;
  PistonAccess_t writer;
};

// Byte-granularity shadow memory, allocated a page at a time.
class PistonShadowMem {
  static constexpr unsigned LG_PAGE = 12;
  static constexpr uintptr_t PAGE_SIZE = uintptr_t(1) << LG_PAGE;

  std::unordered_map<uintptr_t, PistonShadowEntry_t *> pages;
  // Cache of the most recently used page.
  uintptr_t last_page_num = ~uintptr_t(0);
  PistonShadowEntry_t *last_page = nullptr;

  PistonShadowEntry_t *find_page(uintptr_t page_num) {
    if (page_num == last_page_num)
      return last_page;
    auto it = pages.find(page_num);
    if (it == pages.end())
      return nullptr;
    last_page_num = page_num;
    last_page = it->second;
    return last_page;
  }

public:
  ~PistonShadowMem() {
    for (auto &p : pages)
      delete[] p.second;
  }

  // Get the entry for addr, allocating its page if necessary.
  PistonShadowEntry_t &get(uintptr_t addr) {
    uintptr_t page_num = addr >> LG_PAGE;
    PistonShadowEntry_t *page = find_page(page_num);
    if (!page) {
      page = new PistonShadowEntry_t[PAGE_SIZE];
      pages.emplace(page_num, page);
      last_page_num = page_num;
      last_page = page;
    }
    return page[addr & (PAGE_SIZE - 1)];
  }

  // Forget all accesses to [start, start + size).
  void clear(uintptr_t start, size_t size) {
    uintptr_t end = start + size;
    while (start < end) {
      uintptr_t page_end = (start | (PAGE_SIZE - 1)) + 1;
      uintptr_t stop = page_end < end ? page_end : end;
      if (PistonShadowEntry_t *page = find_page(start >> LG_PAGE)) {
        for (uintptr_t a = start; a < stop; ++a) {
          PistonShadowEntry_t &e = page[a & (PAGE_SIZE - 1)];
          e.reader.clear();
          e.writer.clear();
        }
      }
      start = stop;
    }
  }
};

#endif // __PISTON_SHADOW_H__

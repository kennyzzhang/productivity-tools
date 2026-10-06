// -*- C++ -*-
#ifndef __FRAME_DATA_H__
#define __FRAME_DATA_H__

#include "hypertable.h"

enum class EntryFrameType : uint8_t {
  NONE = 0,
  // Low two bits denote the entry type.
  SPAWNER = 1,
  HELPER = 2,
  DETACHER = 3,
  ENTRY_MASK = 3,
  // Next two bits denote the frame type
  SHADOW_FRAME = 1 << 2,
  FULL_FRAME = 2 << 2,
  LOOP_FRAME = 3 << 2,
  FRAME_MASK = 3 << 2,
  // Convenient combined values for entry and frame types.
  SPAWNER_SHADOW_FRAME = SPAWNER | SHADOW_FRAME,
  DETACHER_SHADOW_FRAME = DETACHER | SHADOW_FRAME,
};

static inline bool isSpawner(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) & static_cast<int>(EntryFrameType::SPAWNER)) ==
         static_cast<int>(EntryFrameType::SPAWNER);
}
static inline bool isHelper(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) & static_cast<int>(EntryFrameType::HELPER)) ==
         static_cast<int>(EntryFrameType::HELPER);
}
static inline bool isDetacher(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) & static_cast<int>(EntryFrameType::DETACHER)) ==
         static_cast<int>(EntryFrameType::DETACHER);
}
static inline bool isShadowFrame(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) &
          static_cast<int>(EntryFrameType::SHADOW_FRAME)) ==
         static_cast<int>(EntryFrameType::SHADOW_FRAME);
}
static inline bool isFullFrame(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) &
          static_cast<int>(EntryFrameType::FULL_FRAME)) ==
         static_cast<int>(EntryFrameType::FULL_FRAME);
}
static inline bool isLoopFrame(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) &
          static_cast<int>(EntryFrameType::LOOP_FRAME)) ==
         static_cast<int>(EntryFrameType::LOOP_FRAME);
}
static inline EntryFrameType setSpawner(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::ENTRY_MASK)) |
      static_cast<int>(EntryFrameType::SPAWNER));
}
static inline EntryFrameType setHelper(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::ENTRY_MASK)) |
      static_cast<int>(EntryFrameType::HELPER));
}
static inline EntryFrameType setDetacher(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::ENTRY_MASK)) |
      static_cast<int>(EntryFrameType::DETACHER));
}
static inline EntryFrameType setShadowFrame(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::FRAME_MASK)) |
      static_cast<int>(EntryFrameType::SHADOW_FRAME));
}
static inline EntryFrameType setFullFrame(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::FRAME_MASK)) |
      static_cast<int>(EntryFrameType::FULL_FRAME));
}
static inline EntryFrameType setLoopFrame(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::FRAME_MASK)) |
      static_cast<int>(EntryFrameType::LOOP_FRAME));
}

// Struct for keeping track of shadow frame
struct FrameData_t {
  EntryFrameType frame_data;
  // Whether the current instruction is in a continuation in this frame.
  uint8_t InContin = 0;
  // If this frame was called from the continuation of an ancestor, identifies
  // that ancestor.  Otherwise equals 0.
  uint32_t ParentContin = 0;
  hyper_table *reducer_views = nullptr;

  // PISTON: depth of this frame's S node in the SP-tree walk, and how many
  // steps up the walk to take after returning to that depth when the frame
  // exits (1 for spawned tasks and parallel loops, to leave their P node).
  int32_t entry_depth = 0;
  uint8_t exit_decs = 0;
  // PISTON: for each sync region with outstanding spawns, the depth of the S
  // node that a sync returns to; -1 if the region has nothing to sync.
  static constexpr unsigned MAX_SYNC_REG = 8;
  int32_t sync_base[MAX_SYNC_REG] = {-1, -1, -1, -1, -1, -1, -1, -1};

  // fields that are for debugging purpose only
#if CILKSAN_DEBUG
  uint64_t frame_id;
#endif

  // This function, not the FrameData_t destructor, is the primary way in which
  // frames are deinitialized.  Remember to update this function whenever new
  // fields are added.
  void reset() {
    InContin = 0;
    set_parent_continuation(0);
    // reducer_views = nullptr;
  }

  FrameData_t() = default;
  FrameData_t(const FrameData_t &copy) = delete;

  ~FrameData_t() {
    // update ref counts
    reset();
  }

  // This function, not the FrameData_t constructor, is the primary way in which
  // frames are initialized.  Remember to update this function whenever new
  // fields are added.
  inline void init_new_function(int32_t depth) {
    entry_depth = depth;
    exit_decs = 0;
    clear_sync_bases();
  }

  void clear_sync_bases() {
    for (unsigned i = 0; i < MAX_SYNC_REG; ++i)
      sync_base[i] = -1;
  }

  bool in_continuation() const { return InContin != 0; }
  uint32_t get_parent_continuation() const { return ParentContin; }
  hyper_table *get_or_create_reducer_views() {
    if (!reducer_views)
      reducer_views = new hyper_table;
    return reducer_views;
  }

  // Bits of InContin identify different types of continuations:
  //   Bit 0 - the computation is in the continuation of a parallel loop.
  //   Bit x > 0 - the computation is in an ordinary continuation for a
  //     particular sync region.
  void enter_loop_continuation() { InContin |= 0x1; }
  void exit_loop_continuation() { InContin &= ~0x1; }
  void enter_continuation(const unsigned sync_reg) {
    cilksan_assert(sync_reg < 7 &&
                   "Error marking continuation.  Please report this issue.");
    InContin |= (0x2 << sync_reg);
  }
  void exit_continuation(const unsigned sync_reg) {
    cilksan_assert(sync_reg < 7 &&
                   "Error marking continuation.  Please report this issue.");
    InContin &= ~(0x2 << sync_reg);
  }
  void set_parent_continuation(uint32_t c) { ParentContin = c; }
  void set_or_merge_reducer_views(CilkSanImpl_t *__restrict__ tool,
                                  hyper_table *__restrict__ right_table) {
    reducer_views =
        hyper_table::merge_two_hyper_tables(tool, reducer_views, right_table);
  }

  bool is_loop_frame() const { return isLoopFrame(frame_data); }

  FrameData_t &operator=(FrameData_t &&that) {
    frame_data = that.frame_data;
    InContin = that.InContin;
    ParentContin = that.ParentContin;
    reducer_views = that.reducer_views;
    entry_depth = that.entry_depth;
    exit_decs = that.exit_decs;
    for (unsigned i = 0; i < MAX_SYNC_REG; ++i)
      sync_base[i] = that.sync_base[i];

    that.InContin = 0;
    that.ParentContin = 0;
    that.reducer_views = nullptr;

    return *this;
  }
};

#endif // __FRAME_DATA_H__

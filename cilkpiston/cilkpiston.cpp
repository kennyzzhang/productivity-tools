#include "cilkpiston_internal.h"
#include "debug_util.h"
#include "driver.h"
#include "frame_data.h"
#include "piston_shadow.h"
#include "stack.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>

// FILE io used to print error messages
FILE *err_io = stderr;

#if CILKSAN_DEBUG
enum EventType_t last_event = NONE;
#endif

// Flag to track whether Cilksan is initialized.
bool CILKSAN_INITIALIZED = false;

csi_id_t total_call = 0;
csi_id_t total_spawn = 0;
csi_id_t total_loop = 0;
csi_id_t total_load = 0;
csi_id_t total_store = 0;
csi_id_t total_alloca = 0;
csi_id_t total_allocfn = 0;
csi_id_t total_free = 0;

// declared in print_addr.cpp
extern uintptr_t *call_pc;
extern uintptr_t *load_pc;
extern uintptr_t *store_pc;

// Flag to globally enable/disable instrumentation.
bool instrumentation = false;

// Flag to check if Cilksan is running under RR.
bool is_running_under_rr = false;

// Reentrant flag for enabling/disabling instrumentation; 0 enables checking.
int checking_disabled = 0;

// Stack structure for tracking whether the current execution is parallel, i.e.,
// whether there are any unsynced spawns in the program execution.
Stack_t<uint8_t> parallel_execution;
Stack_t<bool> spbags_frame_skipped;

// Storage for old values of stack_low_addr and stack_high_addr, saved when
// entering a cilkified region.
uintptr_t uncilkified_stack_low_addr = (uintptr_t)-1;
uintptr_t uncilkified_stack_high_addr = 0;

// Stack for tracking whether a stack switch has occurred.
Stack_t<uint8_t> switched_stack;

// Stack structures for keeping track of MAAPs for pointer arguments to function
// calls.
Stack_t<std::pair<csi_id_t, MAAP_t>> MAAPs;
Stack_t<unsigned> MAAP_counts;

// Raise a link-time error if the user attempts to use Cilksan with -static.
//
// This trick is copied from AddressSanitizer.  We only use this trick on Linux,
// since static linking is not supported on MacOSX anyway.
#ifdef __linux__
#include <link.h>
void *CilksanDoesNotSupportStaticLinkage() {
  // This will fail to link with -static.
  return &_DYNAMIC;  // defined in link.h
}
#endif // __linux__

// --------------------- stuff from racedetector ---------------------------

// -------------------------------------------------------------------------
//  Analysis data structures and fields
// -------------------------------------------------------------------------

// Code to handle references to the stack.

// Free list for call-stack nodes
call_stack_node_t *call_stack_node_t::free_list = nullptr;


// Range of stack used by the process
uintptr_t stack_low_addr = (uintptr_t)-1;
uintptr_t stack_high_addr = 0;

// Global object to manage Cilksan data structures.
CilkSanImpl_t CilkSanImpl;

////////////////////////////////////////////////////////////////////////
// Events functions
////////////////////////////////////////////////////////////////////////

/// Helper function for handling the start of a new function.  This
/// function can be a spawned or called Cilk function or a spawned C
/// function.  A called C function is treated as inlined.
inline void CilkSanImpl_t::start_new_function(unsigned num_sync_reg) {
  frame_id++;
  frame_stack.push();

  DBG_TRACE(CALLBACK, "Enter frame %ld, ", frame_id);

  // Get the parent pointer after we push, because once pused, the
  // pointer may no longer be valid due to resize.
  FrameData_t *parent = frame_stack.ancestor(1);
  FrameData_t *child = frame_stack.head();

  cilksan_assert(num_sync_reg <= FrameData_t::MAX_SYNC_REG &&
                 "Too many sync regions in one function.");
  child->init_new_function(depth);

  if (parent->in_continuation())
    child->set_parent_continuation(1);
  else {
    uint32_t parent_contin = parent->get_parent_continuation();
    if (parent_contin > 0)
      child->set_parent_continuation(parent_contin + 1);
    else
      child->set_parent_continuation(0);
  }
  cilksan_assert(!child->in_continuation() &&
                 "New function marked as in-continuation");
  cilksan_assert(child->reducer_views == nullptr &&
                 "New function has non-null table of reducer views");

  WHEN_CILKSAN_DEBUG(frame_stack.head()->frame_id = frame_id);
}

/// Helper function for exiting a function; counterpart of start_new_function.
inline void CilkSanImpl_t::exit_function() {
  FrameData_t *f = frame_stack.head();
  // Close any P nodes this frame left open, then leave the frame's own node.
  unwind_to(f->entry_depth);
  for (unsigned i = 0; i < f->exit_decs; ++i)
    step_out();

  // Popping doesn't actually destruct the object so we need to
  // manually dec the ref counts here.
  f->reset();
  frame_stack.pop();
}

/// Action performed on entering a Cilk function (excluding spawn helper).
inline void CilkSanImpl_t::enter_cilk_function(unsigned num_sync_reg) {
  DBG_TRACE(CALLBACK, "entering a Cilk function, push frame_stack\n");
  start_new_function(num_sync_reg);
}

/// Action performed on leaving a Cilk function (excluding spawn helper).
inline void CilkSanImpl_t::leave_cilk_function(unsigned sync_reg) {
  DBG_TRACE(CALLBACK,
            "leaving a Cilk function (spawner or helper), pop frame_stack\n");
  exit_function();
}

/// Action performed when returning from a spawned child.
/// (That is, returning from a spawn helper.)
inline void CilkSanImpl_t::return_from_detach(unsigned sync_reg) {
  DBG_TRACE(CALLBACK, "return from detach, pop frame_stack\n");
  cilksan_assert(isDetacher(frame_stack.head()->frame_data));
  // Leaves the spawned child's S node, back up to the spawn's P node.
  exit_function();
}

/// Action performed immediately after passing a sync.
inline void CilkSanImpl_t::complete_sync(unsigned sync_reg) {
  FrameData_t *f = frame_stack.head();
  cilksan_assert(sync_reg < FrameData_t::MAX_SYNC_REG && "Invalid sync_reg");
  int32_t base = f->sync_base[sync_reg];
  if (base < 0)
    return;
  // Close the P nodes of every spawn in this sync region.  Sync regions nest,
  // so any other region opened above base is closed too.
  unwind_to(base);
  for (unsigned i = 0; i < FrameData_t::MAX_SYNC_REG; ++i)
    if (f->sync_base[i] >= base)
      f->sync_base[i] = -1;
}

//---------------------------------------------------------------
// Callback functions
//---------------------------------------------------------------
void CilkSanImpl_t::do_enter(unsigned num_sync_reg) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  DBG_TRACE(CALLBACK, "frame %ld cilk_enter_frame_begin, stack depth %d\n",
            frame_id + 1, frame_stack.size());
  // A called Cilk function is composed in series with its caller, so it runs
  // in the caller's current S node.
  enter_cilk_function(num_sync_reg);
  frame_stack.head()->frame_data = EntryFrameType::SPAWNER_SHADOW_FRAME;
}

void CilkSanImpl_t::do_enter_helper(unsigned num_sync_reg) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  DBG_TRACE(CALLBACK, "frame %ld cilk_enter_helper_begin\n", frame_id + 1);
  // A spawn opens a P node whose children are the spawned task and the
  // continuation.  Step into the P node, then into the task's S node.
  step_in();
  step_in();
  enter_cilk_function(num_sync_reg);
  FrameData_t *f = frame_stack.head();
  f->frame_data = EntryFrameType::DETACHER_SHADOW_FRAME;
  f->exit_decs = 1;
}

void CilkSanImpl_t::do_detach() {
  update_strand_stats();
  DBG_TRACE(CALLBACK, "cilk_detach\n");
}

// Called in the spawning frame just before a spawn in sync region sync_reg.
void CilkSanImpl_t::do_spawn_prepare(unsigned sync_reg) {
  FrameData_t *f = frame_stack.head();
  cilksan_assert(sync_reg < FrameData_t::MAX_SYNC_REG && "Invalid sync_reg");
  // The first spawn since the last sync of this region fixes the depth that
  // the region's next sync returns to.
  if (f->sync_base[sync_reg] < 0)
    f->sync_base[sync_reg] = depth;
}

void CilkSanImpl_t::do_detach_continue(unsigned sync_reg) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  DBG_TRACE(CALLBACK, "cilk_detach_continue\n");

  reduce_local_views();
  update_strand_stats();
  // Returning from the spawned task left us at the spawn's P node; step into
  // the continuation's S node.  (After an implicit sync on an unwind, we are
  // already at an S node.)
  if (depth & 1)
    step_in();
  frame_stack.head()->enter_continuation(sync_reg);
}

void CilkSanImpl_t::do_loop_iteration_begin(unsigned num_sync_reg) {
  DBG_TRACE(CALLBACK, "do_loop_iteration_begin()\n");
  if (start_new_loop) {
    // The first time we enter the loop, create a LOOP_FRAME at the head of
    // frame_stack.  The frame sits at the loop's P node, whose children are
    // the iterations.
    DBG_TRACE(CALLBACK, "starting new loop\n");
    step_in();
    enter_cilk_function(num_sync_reg > 0 ? num_sync_reg : 1);
    FrameData_t *func = frame_stack.head();
    func->frame_data = setLoopFrame(EntryFrameType::DETACHER_SHADOW_FRAME);
    func->exit_decs = 1;
    do_detach();
    start_new_loop = false;
  } else {
    cilksan_assert(in_loop());
    update_strand_stats();
    frame_stack.head()->enter_loop_continuation();
  }
  // Step into this iteration's S node.
  step_in();
}

void CilkSanImpl_t::do_loop_iteration_end() {
  reduce_local_views();
  update_strand_stats();
  FrameData_t *func = frame_stack.head();
  func->exit_loop_continuation();
  DBG_TRACE(CALLBACK, "do_loop_iteration_end()\n");
  cilksan_assert(in_loop());
  // Leave this iteration's S node, back to the loop's P node.
  unwind_to(func->entry_depth + 1);
  step_out();
  func->clear_sync_bases();
}

void CilkSanImpl_t::do_loop_end(unsigned sync_reg) {
  DBG_TRACE(CALLBACK, "do_loop_end()\n");
  if (start_new_loop) {
    // The loop ran no iterations, so no loop frame was created.
    start_new_loop = false;
    return;
  }
  cilksan_assert(in_loop());
  // Return from the loop frame, leaving the loop's P node.
  do_leave(sync_reg);
}

void CilkSanImpl_t::do_sync(unsigned sync_reg) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  update_strand_stats();
  reduce_local_views();
  complete_sync(sync_reg);
  frame_stack.head()->exit_continuation(sync_reg);
}

void CilkSanImpl_t::do_leave(unsigned sync_reg) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  cilksan_assert(frame_stack.size() > 1);

  EntryFrameType EFT = frame_stack.head()->frame_data;
  if (isDetacher(EFT))
    return_from_detach(sync_reg);
  else
    leave_cilk_function(sync_reg);
}

// Check races on memory [addr, addr+mem_size) with this access, then record
// the access in the shadow memory, as Cilksan does: a race occurs when a
// previous access is logically parallel with the current strand, and a
// previous access is replaced only by one that follows it in series.
template <bool is_read>
inline void CilkSanImpl_t::check_and_update(const csi_id_t acc_id,
                                            MAType_t type, uintptr_t addr,
                                            size_t mem_size) {
  piston_t::iterator cur = piston.end();
  AccessLoc_t loc(acc_id, type, call_stack);
  for (uintptr_t a = addr; a < addr + mem_size; ++a) {
    PistonShadowEntry_t &e = shadow_memory->get(a);
    bool writer_parallel =
        e.writer.valid() && parallel_with_current(e.writer.pos);
    if (writer_parallel)
      report_race(e.writer.loc, loc, a, is_read ? WR_RACE : WW_RACE);
    if (is_read) {
      if (!e.reader.valid() || !parallel_with_current(e.reader.pos))
        e.reader.set(cur, loc);
    } else {
      if (e.reader.valid() && parallel_with_current(e.reader.pos))
        report_race(e.reader.loc, loc, a, RW_RACE);
      if (!writer_parallel)
        e.writer.set(cur, loc);
    }
  }
}

void CilkSanImpl_t::record_free(uintptr_t addr, size_t mem_size,
                                csi_id_t acc_id, MAType_t type) {
  // Do nothing for 0-byte frees
  if (!mem_size)
    return;
  // TODO: Lock sets are not supported yet; check locked frees as unlocked.
  check_and_update<false>(acc_id, type, addr, mem_size);
}

template <MAType_t type>
void CilkSanImpl_t::do_read(const csi_id_t load_id, uintptr_t addr,
                            size_t mem_size, unsigned alignment) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  if (collect_stats)
    collect_read_stat(mem_size);

  if (is_on_stack(addr))
    advance_stack_frame(addr);

  if (!mem_size)
    return;
  check_and_update<true>(load_id, type, addr, mem_size);
}

template <MAType_t type>
void CilkSanImpl_t::do_write(const csi_id_t store_id, uintptr_t addr,
                             size_t mem_size, unsigned alignment) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  if (collect_stats)
    collect_write_stat(mem_size);

  if (is_on_stack(addr))
    advance_stack_frame(addr);

  if (!mem_size)
    return;
  check_and_update<false>(store_id, type, addr, mem_size);
}

template void CilkSanImpl_t::do_read<MAType_t::RW>(const csi_id_t id,
                                                   uintptr_t addr, size_t len,
                                                   unsigned alignment);
template void CilkSanImpl_t::do_read<MAType_t::FNRW>(const csi_id_t id,
                                                     uintptr_t addr, size_t len,
                                                     unsigned alignment);
template void CilkSanImpl_t::do_read<MAType_t::ALLOC>(const csi_id_t id,
                                                      uintptr_t addr,
                                                      size_t len,
                                                      unsigned alignment);

template void CilkSanImpl_t::do_write<MAType_t::RW>(const csi_id_t id,
                                                    uintptr_t addr, size_t len,
                                                    unsigned alignment);
template void CilkSanImpl_t::do_write<MAType_t::FNRW>(const csi_id_t id,
                                                      uintptr_t addr,
                                                      size_t len,
                                                      unsigned alignment);
template void CilkSanImpl_t::do_write<MAType_t::ALLOC>(const csi_id_t id,
                                                       uintptr_t addr,
                                                       size_t len,
                                                       unsigned alignment);

// TODO: Lock sets are not supported yet.  Accesses made while holding a lock
// (including atomics, when CILKSAN_CHECK_ATOMICS is set) are not checked or
// recorded, so they cause no false races but can hide real ones.
template <MAType_t type>
void CilkSanImpl_t::do_locked_read(const csi_id_t load_id, uintptr_t addr,
                                   size_t mem_size, unsigned alignment) {
  if (is_on_stack(addr))
    advance_stack_frame(addr);
}

template <MAType_t type>
void CilkSanImpl_t::do_locked_write(const csi_id_t store_id, uintptr_t addr,
                                    size_t mem_size, unsigned alignment) {
  if (is_on_stack(addr))
    advance_stack_frame(addr);
}

template void CilkSanImpl_t::do_locked_read<MAType_t::RW>(
    const csi_id_t load_id, uintptr_t addr, size_t len, unsigned alignment);
template void CilkSanImpl_t::do_locked_read<MAType_t::FNRW>(
    const csi_id_t load_id, uintptr_t addr, size_t len, unsigned alignment);
template void CilkSanImpl_t::do_locked_read<MAType_t::ALLOC>(
    const csi_id_t load_id, uintptr_t addr, size_t len, unsigned alignment);

template void CilkSanImpl_t::do_locked_write<MAType_t::RW>(
    const csi_id_t store_id, uintptr_t addr, size_t len, unsigned alignment);
template void CilkSanImpl_t::do_locked_write<MAType_t::FNRW>(
    const csi_id_t store_id, uintptr_t addr, size_t len, unsigned alignment);
template void CilkSanImpl_t::do_locked_write<MAType_t::ALLOC>(
    const csi_id_t store_id, uintptr_t addr, size_t len, unsigned alignment);

// clear the memory block at [start,start+size) (end is exclusive).
void CilkSanImpl_t::clear_shadow_memory(size_t start, size_t size) {
  if (!size)
    return;
  DBG_TRACE(MEMORY, "cilksan_clear_shadow_memory(%p, %ld)\n", start, size);
  shadow_memory->clear(start, size);
}

// TODO: Allocation sites are not tracked yet, so race reports do not say
// where the racing memory was allocated.
void CilkSanImpl_t::record_alloc(size_t start, size_t size,
                                 csi_id_t alloca_id) {}

void CilkSanImpl_t::clear_alloc(size_t start, size_t size) {}

inline void CilkSanImpl_t::print_stats() {
  std::cout << ",size (bytes),count\n";

  for (std::pair<size_t, uint64_t> reads : num_reads_checked)
    std::cout << "reads," << reads.first << "," << reads.second << "\n";
  std::cout << "total reads,," << total_reads_checked << "\n";

  for (std::pair<size_t, uint64_t> writes : num_writes_checked)
    std::cout << "writes," << writes.first << "," << writes.second << "\n";
  std::cout << "total writes,," << total_writes_checked << "\n";

  std::cout << "total strands,," << strand_count << "\n";

  for (std::pair<size_t, uint64_t> reads : max_num_reads_checked)
    std::cout << "max reads," << reads.first << "," << reads.second << "\n";

  for (std::pair<size_t, uint64_t> writes : max_num_writes_checked)
    std::cout << "max writes," << writes.first << "," << writes.second << "\n";
}

///////////////////////////////////////////////////////////////////////////
// Tool initialization and deinitialization

void CilkSanImpl_t::deinit() {
  static bool deinit = false;
  if (!deinit)
    deinit = true;
  else
    return; // deinit-ed already

  print_race_report();
  // Optionally print statistics.
  if (collect_stats)
    print_stats();
}

// called upon process exit
static void csan_destroy(void) {
  disable_instrumentation();
  disable_checking();
  CilkSanImpl.deinit();
  fflush(stdout);
  if (call_pc) {
    free(call_pc);
    call_pc = nullptr;
  }
  if (spawn_pc) {
    free(spawn_pc);
    spawn_pc = nullptr;
  }
  if (loop_pc) {
    free(loop_pc);
    loop_pc = nullptr;
  }
  if (load_pc) {
    free(load_pc);
    load_pc = nullptr;
  }
  if (store_pc) {
    free(store_pc);
    store_pc = nullptr;
  }
  if (alloca_pc) {
    free(alloca_pc);
    alloca_pc = nullptr;
  }
  if (allocfn_pc) {
    free(allocfn_pc);
    allocfn_pc = nullptr;
  }
  if (allocfn_prop) {
    free(allocfn_prop);
    allocfn_prop = nullptr;
  }
  if (free_pc) {
    free(free_pc);
    free_pc = nullptr;
  }
}

CilkSanImpl_t::~CilkSanImpl_t() {
  csan_destroy();
  CILKSAN_INITIALIZED = false;
}

void CilkSanImpl_t::init() {
  DBG_TRACE(CALLBACK, "cilksan_init()\n");

  // Enable stats collection if requested
  {
    char *e = getenv("CILKSAN_STATS");
    if (e && 0 != strcmp(e, "0"))
      collect_stats = true;
  }
  // Enable checking of atomics if requested
  {
    char *e = getenv("CILKSAN_CHECK_ATOMICS");
    if (e) {
      if (0 == strcmp(e, "0"))
        check_atomics = false;
      else
        check_atomics = true;
    }
  }

  std::cerr << "Running CilkPiston race detector.\n";

  // these are true upon creation of the stack
  cilksan_assert(frame_stack.size() == 1);

  shadow_memory = new PistonShadowMem();
}

#include "cilkpiston_internal.h"
#include "debug_util.h"
#include "driver.h"
#include "frame_data.h"
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
}

/// Helper function for exiting a function; counterpart of start_new_function.
inline void CilkSanImpl_t::exit_function() {
}

/// Action performed on entering a Cilk function (excluding spawn helper).
inline void CilkSanImpl_t::enter_cilk_function(unsigned num_sync_reg) {
}

/// Action performed on leaving a Cilk function (excluding spawn helper).
inline void CilkSanImpl_t::leave_cilk_function(unsigned sync_reg) {
}

/// Action performed when returning from a spawned child.
/// (That is, returning from a spawn helper.)
inline void CilkSanImpl_t::return_from_detach(unsigned sync_reg) {
}

/// Action performed immediately after passing a sync.
inline void CilkSanImpl_t::complete_sync(unsigned sync_reg) {
}

//---------------------------------------------------------------
// Callback functions
//---------------------------------------------------------------
void CilkSanImpl_t::do_enter(unsigned num_sync_reg) {
  return; //TODO: [kzz]
}

void CilkSanImpl_t::do_enter_helper(unsigned num_sync_reg) {
  return; //TODO: [kzz]
}

void CilkSanImpl_t::do_detach() {
  return; //TODO: [kzz]
}

void CilkSanImpl_t::do_detach_continue(unsigned sync_reg) {
  return; //TODO: [kzz]
}

void CilkSanImpl_t::do_loop_iteration_begin(unsigned num_sync_reg) {
  return; //TODO: [kzz]
}

void CilkSanImpl_t::do_loop_iteration_end() {
  return; //TODO: [kzz]
}

void CilkSanImpl_t::do_loop_end(unsigned sync_reg) {
  return; //TODO: [kzz]
}

void CilkSanImpl_t::do_sync(unsigned sync_reg) {
  return; //TODO: [kzz]
}

void CilkSanImpl_t::do_leave(unsigned sync_reg) {
  return; //TODO: [kzz]
}

void CilkSanImpl_t::record_free(uintptr_t addr, size_t mem_size,
                                csi_id_t acc_id, MAType_t type) {
  return; //TODO: [kzz]
}

// Check races on memory [addr, addr+mem_size) with this read access.  Once done
// checking, update shadow_memory with this new read access.
__attribute__((always_inline)) void check_races_and_update_with_read(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, SimpleShadowMem &shadow_memory) {
  return; //TODO: [kzz]
}

// Check races on memory [addr, addr+mem_size) with this write access.  Once
// done checking, update shadow_memory with this new read access.  Very similar
// to check_races_and_update_with_read function.
__attribute__((always_inline)) void check_races_and_update_with_write(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, SimpleShadowMem &shadow_memory) {
  return; //TODO: [kzz]
}

// Check races on memory [addr, addr+mem_size) with this memory access.  Once
// done checking, update shadow_memory with the new access.
//
// is_read: whether or not this access reads memory
// acc_id: ID of the memory-access instruction
// type: type of memory access, e.g., a read/write, an allocation, a free
// addr: memory address accessed
// mem_size: number of bytes accessed, starting at addr
// f: pointer to current frame on the shadow stack
// shadow_memory: shadow memory recording memory access information
template <bool is_read>
void check_races_and_update(const csi_id_t acc_id, MAType_t type,
                            uintptr_t addr, size_t mem_size, FrameData_t *f,
                            SimpleShadowMem &shadow_memory) {
  return; //TODO: [kzz]
}

// Check data races on memory [addr, addr+mem_size) with this read access.  Once
// done checking, update shadow_memory with this new read access.
__attribute__((always_inline)) void check_data_races_and_update_with_read(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, const LockSet_t &lockset, SimpleShadowMem &shadow_memory) {
  return; //TODO: [kzz]
}

// Check data races on memory [addr, addr+mem_size) with this write access.
// Once done checking, update shadow_memory with this new read access.  Very
// similar to check_data_races_and_update_with_read function.
__attribute__((always_inline)) void check_data_races_and_update_with_write(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, const LockSet_t &lockset, SimpleShadowMem &shadow_memory) {
  return; //TODO: [kzz]
}

// Check for data races on memory [addr, addr+mem_size) with this memory access.
// Once done checking, update shadow_memory with the new access.
//
// is_read: whether or not this access reads memory
// acc_id: ID of the memory-access instruction
// type: type of memory access, e.g., a read/write, an allocation, a free
// addr: memory address accessed
// mem_size: number of bytes accessed, starting at addr
// f: pointer to current frame on the shadow stack
// lockset: set of currently held locks
// shadow_memory: shadow memory recording memory access information
template <bool is_read>
void check_data_races_and_update(const csi_id_t acc_id, MAType_t type,
                                 uintptr_t addr, size_t mem_size, FrameData_t *f,
                                 const LockSet_t &lockset,
                                 SimpleShadowMem &shadow_memory) {
  return; //TODO: [kzz]
}

template <MAType_t type>
void CilkSanImpl_t::do_read(const csi_id_t load_id, uintptr_t addr,
                            size_t mem_size, unsigned alignment) {
  return; //TODO: [kzz]
}

template <MAType_t type>
void CilkSanImpl_t::do_write(const csi_id_t store_id, uintptr_t addr,
                             size_t mem_size, unsigned alignment) {
  return; //TODO: [kzz]
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

template <MAType_t type>
void CilkSanImpl_t::do_locked_read(const csi_id_t load_id, uintptr_t addr,
                                   size_t mem_size, unsigned alignment) {
  return; //TODO: [kzz]
}

template <MAType_t type>
void CilkSanImpl_t::do_locked_write(const csi_id_t store_id, uintptr_t addr,
                                    size_t mem_size, unsigned alignment) {
  return; //TODO: [kzz]
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
  return; //TODO: [kzz]

  if (!size)
    return;
  DBG_TRACE(MEMORY, "cilksan_clear_shadow_memory(%p, %ld)\n", start, size);
}

void CilkSanImpl_t::record_alloc(size_t start, size_t size,
                                 csi_id_t alloca_id) {
  return; //TODO: [kzz]
}

void CilkSanImpl_t::clear_alloc(size_t start, size_t size) {
  return; //TODO: [kzz]
}

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

  std::cerr << "Running Cilksan race detector.\n";

  // these are true upon creation of the stack
  cilksan_assert(frame_stack.size() == 1);
}

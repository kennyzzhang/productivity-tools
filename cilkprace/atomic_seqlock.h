#ifndef _ATOMIC_SEQLOCK_H
#define _ATOMIC_SEQLOCK_H

#pragma GCC visibility push(default)

#include <cilk/cilkprace_ablation.h>

#include <atomic>
#include <cstdint>
#include <sched.h>

// !CILKPRACE_ABL_SEQLOCK selects the no-op seqlock below to measure what
// synchronization costs. That is only sound with a single worker; see
// cilkprace_ablation.h.
#if (defined(SERIAL_TOOL) && SERIAL_TOOL) || defined(SERIAL) ||                \
    defined(CILK_SERIAL) || !CILKPRACE_ABL_SEQLOCK

class atomic_seqlock {
  class write_lock_t {
  public:
    atomic_seqlock& seqlock;
    write_lock_t(atomic_seqlock& seqlock) : seqlock(seqlock) {}
    void lock() {}
    void unlock() {}
  };

public:
  inline void begin_write() {}
  inline void end_write() {}
  inline uint32_t begin_read() { return 0; }
  inline bool read_was_safe(uint32_t old_seq) const { return true; }
  inline write_lock_t write_lock() { return *this; }
};

#else

class atomic_seqlock {
  // We store a has_writer boolean in the low-order bit
  std::atomic<uint32_t> seq{0};

  class write_lock_t {
  public:
    atomic_seqlock& seqlock;
    write_lock_t(atomic_seqlock& seqlock) : seqlock(seqlock) {}
    void lock() { seqlock.begin_write(); }
    void unlock() { seqlock.end_write(); }
  };

public:
  inline void begin_write() {
      while (true) {
          uint32_t s = seq.load(std::memory_order_relaxed);
          if ((s & 1) == 0) { // no writer
              if (seq.compare_exchange_weak(s, s + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                  break;
              }
          }
          // Spin on read to avoid thrashing
          while (seq.load(std::memory_order_relaxed) & 1) {
              #if defined(__x86_64__) || defined(__i386__)
              __builtin_ia32_pause();
              #elif defined(__aarch64__)
              __builtin_arm_yield();
              #endif
          }
      }
  }

  inline void end_write() {
      // Our write is visible to us-- we can just load-increment weakly
      seq.store(seq.load(std::memory_order_relaxed) + 1, std::memory_order_release);
  }

  inline write_lock_t write_lock() {
    return *this;
  }

  inline uint32_t begin_read() {
      uint32_t ret = seq.load(std::memory_order_relaxed);
      while (__builtin_expect(ret & 1, 0)) {
          #if defined(__aarch64__)
          __builtin_arm_yield();
          #elif defined(__x86_64__) || defined(__i386__)
          __builtin_ia32_pause();
          #endif
          ret = seq.load(std::memory_order_relaxed);
      }
      return ret;
  }

  inline bool read_was_safe(uint32_t old_seq) const {
      return seq.load(std::memory_order_acquire) == old_seq;
  }
};

#endif // !SERIAL_TOOL

#pragma GCC visibility pop

#endif // _ATOMIC_SEQLOCK_H

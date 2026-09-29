#ifndef TIMING_H
#define TIMING_H

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

/* RD_TIMED_ONLY=1 race-checks only the timed region: checking is turned off
   at startup and on only between timer_start() and timer_stop_ns(), so setup
   and verification run unchecked. Uses the race detector's
   __cilksan_{enable,disable}_checking, looked up at run time so builds
   without a race detector still link; without one this does nothing. */
static void (*_rd_enable)(void), (*_rd_disable)(void);

__attribute__((constructor)) static void _rd_timed_only_init(void) {
    const char *e = getenv("RD_TIMED_ONLY");
    if (!e || e[0] != '1')
        return;
    _rd_enable = (void (*)(void))dlsym(RTLD_DEFAULT, "__cilksan_enable_checking");
    _rd_disable = (void (*)(void))dlsym(RTLD_DEFAULT, "__cilksan_disable_checking");
    if (!_rd_enable || !_rd_disable) {
        _rd_enable = _rd_disable = NULL;
        return;
    }
    _rd_disable();
}

/* CLOCK_MONOTONIC_RAW: nanosecond resolution on Linux and macOS (macOS's
   CLOCK_MONOTONIC has only microseconds). Times are kept in nanoseconds. */
static struct timespec _timer_start, _timer_end;

static inline void timer_start(void) {
    if (_rd_enable)
        _rd_enable();
    clock_gettime(CLOCK_MONOTONIC_RAW, &_timer_start);
}

static inline unsigned long long timer_stop_ns(void) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &_timer_end);
    if (_rd_disable)
        _rd_disable();
    unsigned long long start_ns = (unsigned long long)_timer_start.tv_sec * 1000000000ULL + (unsigned long long)_timer_start.tv_nsec;
    unsigned long long end_ns = (unsigned long long)_timer_end.tv_sec * 1000000000ULL + (unsigned long long)_timer_end.tv_nsec;
    return end_ns - start_ns;
}

static unsigned long long _min_time_ns = (unsigned long long)-1;

static inline void record_time(unsigned long long cur_time_ns) {
    if (cur_time_ns < _min_time_ns) {
        _min_time_ns = cur_time_ns;
    }
}

static inline void report_time(void) {
    printf("%.9f\n", _min_time_ns / 1e9);
}

#endif

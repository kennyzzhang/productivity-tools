#ifndef TIMING_H
#define TIMING_H

#include <time.h>
#include <stdio.h>

static struct timespec _timer_start, _timer_end;

static inline void timer_start(void) {
    clock_gettime(CLOCK_MONOTONIC, &_timer_start);
}

static inline unsigned long long timer_stop_ms(void) {
    clock_gettime(CLOCK_MONOTONIC, &_timer_end);
    unsigned long long start_ms = (unsigned long long)_timer_start.tv_sec * 1000ULL + (unsigned long long)_timer_start.tv_nsec / 1000000ULL;
    unsigned long long end_ms = (unsigned long long)_timer_end.tv_sec * 1000ULL + (unsigned long long)_timer_end.tv_nsec / 1000000ULL;
    return end_ms - start_ms;
}

static unsigned long long _min_time_ms = (unsigned long long)-1;

static inline void record_time(unsigned long long cur_time_ms) {
    if (_min_time_ms == (unsigned long long)-1 || cur_time_ms < _min_time_ms) {
        _min_time_ms = cur_time_ms;
    }
}

static inline void report_time(void) {
    printf("%f\n", _min_time_ms / 1000.0);
}

#endif

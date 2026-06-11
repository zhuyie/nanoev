#ifndef NANOEV_BENCH_CLOCK_H
#define NANOEV_BENCH_CLOCK_H

#include <stdint.h>

typedef struct bench_timeval {
    uint64_t tv_sec;
    uint32_t tv_usec;
} bench_timeval;

void bench_now(bench_timeval *now);
uint64_t bench_time_us(void);
uint64_t bench_time_diff_ms(const bench_timeval *start, const bench_timeval *end);

#endif

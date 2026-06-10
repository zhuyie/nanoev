#ifndef NANOEV_BENCH_STATS_H
#define NANOEV_BENCH_STATS_H

#include "nanoev.h"
#include <stdint.h>

#define BENCH_LATENCY_BUCKETS 32

typedef struct bench_stats {
    uint64_t requests;
    uint64_t bytes;
    uint64_t errors;
    uint64_t latency_count;
    uint64_t latency_sum_us;
    uint64_t latency_min_us;
    uint64_t latency_max_us;
    uint64_t latency_buckets[BENCH_LATENCY_BUCKETS];
} bench_stats;

void bench_stats_init(bench_stats *stats);
void bench_stats_record_request(bench_stats *stats, uint64_t bytes);
void bench_stats_record_error(bench_stats *stats);
void bench_stats_record_latency(bench_stats *stats, uint64_t latency_us);
void bench_stats_print_delta_header(const char *prefix);
void bench_stats_print_delta(const char *prefix, const bench_stats *stats, const bench_stats *previous, uint64_t elapsed_ms);
void bench_stats_print_total(const char *prefix, const bench_stats *stats, uint64_t elapsed_ms);
uint64_t bench_time_us(void);
uint64_t bench_time_diff_ms(const nanoev_timeval *start, const nanoev_timeval *end);

#endif

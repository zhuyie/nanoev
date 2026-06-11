#ifndef NANOEV_BENCH_STATS_H
#define NANOEV_BENCH_STATS_H

#include <stdint.h>

#define BENCH_LATENCY_BUCKETS 32

typedef struct bench_stats {
    uint64_t requests;
    uint64_t bytes;
    uint64_t errors;
    uint64_t accept_errors;
    uint64_t io_errors;
    uint64_t latency_count;
    uint64_t latency_sum_us;
    uint64_t latency_min_us;
    uint64_t latency_max_us;
    uint64_t latency_buckets[BENCH_LATENCY_BUCKETS];
} bench_stats;

void bench_stats_init(bench_stats *stats);
void bench_stats_record_request(bench_stats *stats, uint64_t bytes);
void bench_stats_record_error(bench_stats *stats);
void bench_stats_record_accept_error(bench_stats *stats);
void bench_stats_record_io_error(bench_stats *stats);
void bench_stats_record_latency(bench_stats *stats, uint64_t latency_us);
void bench_stats_print_delta_header(const char *prefix, int show_error_breakdown);
void bench_stats_print_delta(const char *prefix, const bench_stats *stats, const bench_stats *previous,
    uint64_t elapsed_ms, int show_error_breakdown);
void bench_stats_print_total(const char *prefix, const bench_stats *stats, uint64_t elapsed_ms,
    int show_error_breakdown);
#endif

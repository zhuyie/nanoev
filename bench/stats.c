#include "stats.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FORMAT_BUFFER_SIZE 32

static unsigned int latency_bucket(uint64_t latency_us)
{
    unsigned int bucket = 0;
    uint64_t upper = 1;

    while (bucket + 1 < BENCH_LATENCY_BUCKETS && latency_us > upper) {
        upper <<= 1;
        bucket++;
    }
    return bucket;
}

static uint64_t percentile_us(const bench_stats *stats, unsigned int percentile)
{
    uint64_t target;
    uint64_t seen = 0;
    unsigned int i;

    if (!stats->latency_count)
        return 0;

    target = (stats->latency_count * percentile + 99) / 100;
    if (!target)
        target = 1;

    for (i = 0; i < BENCH_LATENCY_BUCKETS; i++) {
        seen += stats->latency_buckets[i];
        if (seen >= target) {
            if (!i)
                return 1;
            return (uint64_t)1 << i;
        }
    }

    return stats->latency_max_us;
}

static void format_count(uint64_t value, char *buf, size_t size)
{
    char raw[FORMAT_BUFFER_SIZE];
    int raw_len;
    int first_group;
    int i;
    int out = 0;

    snprintf(raw, sizeof(raw), "%llu", (unsigned long long)value);
    raw_len = (int)strlen(raw);
    first_group = raw_len % 3;
    if (!first_group)
        first_group = 3;

    for (i = 0; i < raw_len && out + 1 < (int)size; i++) {
        if (i > 0 && (i - first_group) % 3 == 0 && out + 1 < (int)size)
            buf[out++] = ',';
        buf[out++] = raw[i];
    }
    buf[out] = '\0';
}

static void format_rate(double value, char *buf, size_t size)
{
    if (value >= 1000000000.0) {
        snprintf(buf, size, "%.2fG/s", value / 1000000000.0);
    } else if (value >= 1000000.0) {
        snprintf(buf, size, "%.2fM/s", value / 1000000.0);
    } else if (value >= 1000.0) {
        snprintf(buf, size, "%.2fk/s", value / 1000.0);
    } else {
        snprintf(buf, size, "%.2f/s", value);
    }
}

static void format_mib(double value, char *buf, size_t size)
{
    snprintf(buf, size, "%.2f", value);
}

static void format_clock_time(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_now;

#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif

    snprintf(buf, size, "%02d:%02d:%02d", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

void bench_stats_init(bench_stats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->latency_min_us = UINT64_MAX;
}

void bench_stats_record_request(bench_stats *stats, uint64_t bytes)
{
    stats->requests++;
    stats->bytes += bytes;
}

void bench_stats_record_error(bench_stats *stats)
{
    bench_stats_record_io_error(stats);
}

void bench_stats_record_accept_error(bench_stats *stats)
{
    stats->errors++;
    stats->accept_errors++;
}

void bench_stats_record_io_error(bench_stats *stats)
{
    stats->errors++;
    stats->io_errors++;
}

void bench_stats_record_latency(bench_stats *stats, uint64_t latency_us)
{
    unsigned int bucket = latency_bucket(latency_us);

    stats->latency_count++;
    stats->latency_sum_us += latency_us;
    if (latency_us < stats->latency_min_us)
        stats->latency_min_us = latency_us;
    if (latency_us > stats->latency_max_us)
        stats->latency_max_us = latency_us;
    stats->latency_buckets[bucket]++;
}

void bench_stats_print_delta_header(const char *prefix, int show_error_breakdown)
{
    (void)show_error_breakdown;

    printf("\n[%s] interval stats\n", prefix);
    printf("  %-8s %12s %10s %12s %14s %10s\n",
        "time", "req/s", "MiB", "requests", "total", "errors");
}

void bench_stats_print_delta(const char *prefix, const bench_stats *stats, const bench_stats *previous,
    uint64_t elapsed_ms, int show_error_breakdown)
{
    uint64_t requests = stats->requests - previous->requests;
    uint64_t bytes = stats->bytes - previous->bytes;
    uint64_t errors = stats->errors - previous->errors;
    double seconds = elapsed_ms ? (double)elapsed_ms / 1000.0 : 1.0;
    char time_buf[FORMAT_BUFFER_SIZE];
    char rate_buf[FORMAT_BUFFER_SIZE];
    char mib_buf[FORMAT_BUFFER_SIZE];
    char requests_buf[FORMAT_BUFFER_SIZE];
    char total_buf[FORMAT_BUFFER_SIZE];
    char errors_buf[FORMAT_BUFFER_SIZE];

    (void)show_error_breakdown;

    format_clock_time(time_buf, sizeof(time_buf));
    format_rate((double)requests / seconds, rate_buf, sizeof(rate_buf));
    format_mib((double)bytes / (1024.0 * 1024.0), mib_buf, sizeof(mib_buf));
    format_count(requests, requests_buf, sizeof(requests_buf));
    format_count(stats->requests, total_buf, sizeof(total_buf));
    format_count(errors, errors_buf, sizeof(errors_buf));

    printf("  %-8s %12s %10s %12s %14s %10s\n",
        time_buf, rate_buf, mib_buf, requests_buf, total_buf, errors_buf);
}

void bench_stats_print_total(const char *prefix, const bench_stats *stats, uint64_t elapsed_ms,
    int show_error_breakdown)
{
    double seconds = elapsed_ms ? (double)elapsed_ms / 1000.0 : 1.0;
    uint64_t avg = stats->latency_count ? stats->latency_sum_us / stats->latency_count : 0;
    uint64_t min = stats->latency_min_us == UINT64_MAX ? 0 : stats->latency_min_us;
    char duration_buf[FORMAT_BUFFER_SIZE];
    char requests_buf[FORMAT_BUFFER_SIZE];
    char errors_buf[FORMAT_BUFFER_SIZE];
    char accept_errors_buf[FORMAT_BUFFER_SIZE];
    char io_errors_buf[FORMAT_BUFFER_SIZE];
    char rate_buf[FORMAT_BUFFER_SIZE];
    char mib_buf[FORMAT_BUFFER_SIZE];
    char mib_rate_buf[FORMAT_BUFFER_SIZE];

    if (elapsed_ms >= 1000) {
        snprintf(duration_buf, sizeof(duration_buf), "%.2fs", (double)elapsed_ms / 1000.0);
    } else {
        snprintf(duration_buf, sizeof(duration_buf), "%llums", (unsigned long long)elapsed_ms);
    }
    format_count(stats->requests, requests_buf, sizeof(requests_buf));
    format_count(stats->errors, errors_buf, sizeof(errors_buf));
    format_count(stats->accept_errors, accept_errors_buf, sizeof(accept_errors_buf));
    format_count(stats->io_errors, io_errors_buf, sizeof(io_errors_buf));
    format_rate((double)stats->requests / seconds, rate_buf, sizeof(rate_buf));
    format_mib((double)stats->bytes / (1024.0 * 1024.0), mib_buf, sizeof(mib_buf));
    format_mib((double)stats->bytes / (1024.0 * 1024.0) / seconds, mib_rate_buf, sizeof(mib_rate_buf));

    printf("\n[%s] summary\n", prefix);
    printf("  duration : %s\n", duration_buf);
    printf("  requests : %s (%s)\n", requests_buf, rate_buf);
    printf("  transfer : %s MiB (%s MiB/s)\n", mib_buf, mib_rate_buf);
    if (show_error_breakdown) {
        printf("  errors   : %s (accept=%s io=%s)\n", errors_buf, accept_errors_buf, io_errors_buf);
    } else {
        printf("  errors   : %s\n", errors_buf);
    }

    if (stats->latency_count) {
        printf("  latency  : min=%lluus avg=%lluus p50=%lluus p95=%lluus p99=%lluus max=%lluus\n",
            (unsigned long long)min,
            (unsigned long long)avg,
            (unsigned long long)percentile_us(stats, 50),
            (unsigned long long)percentile_us(stats, 95),
            (unsigned long long)percentile_us(stats, 99),
            (unsigned long long)stats->latency_max_us);
    }
}

uint64_t bench_time_us(void)
{
    nanoev_timeval now;

    nanoev_now(&now);
    return ((uint64_t)now.tv_sec * 1000000ULL) + (uint64_t)now.tv_usec;
}

uint64_t bench_time_diff_ms(const nanoev_timeval *start, const nanoev_timeval *end)
{
    uint64_t start_ms = ((uint64_t)start->tv_sec * 1000ULL) + ((uint64_t)start->tv_usec / 1000ULL);
    uint64_t end_ms = ((uint64_t)end->tv_sec * 1000ULL) + ((uint64_t)end->tv_usec / 1000ULL);

    return end_ms - start_ms;
}

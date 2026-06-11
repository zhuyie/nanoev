#include "clock.h"

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#else
# include <sys/time.h>
#endif

void bench_now(bench_timeval *now)
{
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    uint64_t us;

    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    us = (uint64_t)((counter.QuadPart * 1000000ULL) / frequency.QuadPart);
    now->tv_sec = us / 1000000ULL;
    now->tv_usec = (uint32_t)(us % 1000000ULL);
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    now->tv_sec = (uint64_t)tv.tv_sec;
    now->tv_usec = (uint32_t)tv.tv_usec;
#endif
}

uint64_t bench_time_us(void)
{
    bench_timeval now;

    bench_now(&now);
    return (now.tv_sec * 1000000ULL) + (uint64_t)now.tv_usec;
}

uint64_t bench_time_diff_ms(const bench_timeval *start, const bench_timeval *end)
{
    uint64_t start_ms = (start->tv_sec * 1000ULL) + ((uint64_t)start->tv_usec / 1000ULL);
    uint64_t end_ms = (end->tv_sec * 1000ULL) + ((uint64_t)end->tv_usec / 1000ULL);

    return end_ms - start_ms;
}

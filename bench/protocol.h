#ifndef NANOEV_BENCH_PROTOCOL_H
#define NANOEV_BENCH_PROTOCOL_H

#include <stdint.h>

#define BENCH_FRAME_HEADER_SIZE 8

static void bench_write_u32(unsigned char *buf, uint32_t value)
{
    buf[0] = (unsigned char)((value >> 24) & 0xff);
    buf[1] = (unsigned char)((value >> 16) & 0xff);
    buf[2] = (unsigned char)((value >> 8) & 0xff);
    buf[3] = (unsigned char)(value & 0xff);
}

static uint32_t bench_read_u32(const unsigned char *buf)
{
    return ((uint32_t)buf[0] << 24)
        | ((uint32_t)buf[1] << 16)
        | ((uint32_t)buf[2] << 8)
        | (uint32_t)buf[3];
}

static void bench_frame_write_header(unsigned char *buf, uint32_t payload_size, uint32_t sequence)
{
    bench_write_u32(buf, payload_size);
    bench_write_u32(buf + 4, sequence);
}

static uint32_t bench_frame_payload_size(const unsigned char *buf)
{
    return bench_read_u32(buf);
}

static uint32_t bench_frame_sequence(const unsigned char *buf)
{
    return bench_read_u32(buf + 4);
}

#endif

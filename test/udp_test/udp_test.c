#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nanoev.h"
#include <assert.h>
#define ASSERT assert

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else

#endif

struct nanoev_addr local_addr;
struct nanoev_addr server_addr;
char read_buf[100];
int times = 0;
int max_times = 1000000;
const char *msg = "ABCD";
unsigned int msg_len = 4;
int read_success_count = 0;
int read_fail_count = 0;
int write_success_count = 0;
int write_fail_count = 0;

void on_read(
    nanoev_event *udp,
    int status,
    void *buf,
    unsigned int bytes,
    const struct nanoev_addr *from_addr
    )
{
    int ret_code;

    if (!status)
        ++read_success_count;
    else
        ++read_fail_count;

    //printf("on_read status=%d bytes=%u data=%s\n", status, bytes, buf);

    ret_code = nanoev_udp_read(udp, read_buf, sizeof(read_buf), on_read);
    ASSERT(ret_code == NANOEV_SUCCESS);
}

void on_write(
    nanoev_event *udp,
    int status,
    void *buf,
    unsigned int bytes
    )
{
    if (!status)
        ++write_success_count;
    else
        ++write_fail_count;

    //printf("on_write status=%d bytes=%u\n", status, bytes);
    
    ++times;
    if (times < max_times) {
        int ret_code = nanoev_udp_write(udp, msg, msg_len, &server_addr, on_write);
        ASSERT(ret_code == NANOEV_SUCCESS);
    } else {
        nanoev_loop_break(nanoev_event_loop(udp));
    }
}

int main(int argc, char* argv[])
{
    int ret_code;
    nanoev_loop *loop;
    nanoev_event *udp;
    struct nanoev_timeval tmStart, tmEnd;
    unsigned int duration;

    ret_code = nanoev_init();
    ASSERT(ret_code == NANOEV_SUCCESS);
    loop = nanoev_loop_new(NULL);
    ASSERT(loop);

    udp = nanoev_event_new(nanoev_event_udp, loop, NULL);
    ASSERT(udp);

    // bind
    nanoev_addr_init(&local_addr, "127.0.0.1", 4000);
    ret_code = nanoev_udp_bind(udp, &local_addr);
    ASSERT(ret_code == NANOEV_SUCCESS);

    nanoev_now(&tmStart);

    ret_code = nanoev_udp_read(udp, read_buf, sizeof(read_buf), on_read);
    ASSERT(ret_code == NANOEV_SUCCESS);

    nanoev_addr_init(&server_addr, "127.0.0.1", 4000);
    ret_code = nanoev_udp_write(udp, msg, msg_len, &server_addr, on_write);
    ASSERT(ret_code == NANOEV_SUCCESS);

    nanoev_loop_run(loop);

    nanoev_now(&tmEnd);
    duration = (tmEnd.tv_sec*1000 + tmEnd.tv_usec/1000) - (tmStart.tv_sec*1000 + tmStart.tv_usec/1000);
    printf("time = %u ms\n", duration);

    printf("read  success=%d fail=%d\n", read_success_count, read_fail_count);
    printf("write success=%d fail=%d\n", write_success_count, write_fail_count);

    nanoev_event_free(udp);

    nanoev_loop_free(loop);

    nanoev_term();

    return 0;
}

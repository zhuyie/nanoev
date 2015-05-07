#include <stdio.h>
#include <stdlib.h>
#include "nanoev.h"
#include <assert.h>
#define ASSERT assert

struct nanoev_addr to_addr;
char read_buf[10000];
int times = 0;

void on_write(
    nanoev_event *udp,
    int status,
    void *buf,
    unsigned int bytes
    )
{
    ++times;
    printf("on_write status=%d bytes=%u\n", status, bytes);
    
    if (times < 5) {
        nanoev_udp_write(udp, "ABCD\n", 5, &to_addr, on_write);
    }
}

void on_read(
    nanoev_event *udp,
    int status,
    void *buf,
    unsigned int bytes,
    const struct nanoev_addr *from_addr
    )
{
    printf("on_read status=%d bytes=%u\n", status, bytes);
}

int main(int argc, char* argv[])
{
    int ret_code;
    nanoev_loop *loop;
    nanoev_event *udp;
    struct nanoev_addr local_addr;

    ret_code = nanoev_init();
    ASSERT(ret_code == NANOEV_SUCCESS);
    loop = nanoev_loop_new(NULL);
    ASSERT(loop);

    udp = nanoev_event_new(nanoev_event_udp, loop, NULL);
    ASSERT(udp);

    nanoev_addr_init(&local_addr, "0.0.0.0", 4001);
    nanoev_udp_bind(udp, &local_addr);
    nanoev_udp_read(udp, read_buf, sizeof(read_buf), on_read);

/*
    nanoev_addr_init(&to_addr, "127.0.0.1", 4000);
    times = 0;

    nanoev_udp_write(udp, "ABCD\n", 5, &to_addr, on_write);
*/
    nanoev_loop_run(loop);

    nanoev_event_free(udp);

    nanoev_loop_free(loop);

    nanoev_term();

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nanoev.h"
#include <assert.h>
#define ASSERT assert

struct nanoev_addr local_addr;
struct nanoev_addr to_addr;
char read_buf[100];
int times = 0;
const char *msg = "ABCD\n";

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
        nanoev_udp_write(udp, msg, (unsigned int)strlen(msg), &to_addr, on_write);
        nanoev_udp_read(udp, read_buf, sizeof(read_buf), on_read);
    }
}

int main(int argc, char* argv[])
{
    int ret_code;
    nanoev_loop *loop;
    nanoev_event *udp;

    ret_code = nanoev_init();
    ASSERT(ret_code == NANOEV_SUCCESS);
    loop = nanoev_loop_new(NULL);
    ASSERT(loop);

    udp = nanoev_event_new(nanoev_event_udp, loop, NULL);
    ASSERT(udp);

    nanoev_addr_init(&local_addr, "127.0.0.1", 4000);
    ret_code = nanoev_udp_bind(udp, &local_addr);
    ASSERT(ret_code == NANOEV_SUCCESS);

    ret_code = nanoev_udp_read(udp, read_buf, sizeof(read_buf), on_read);
    ASSERT(ret_code == NANOEV_SUCCESS);

    nanoev_addr_init(&to_addr, "127.0.0.1", 4000);
    ret_code = nanoev_udp_write(udp, msg, (unsigned int)strlen(msg), &to_addr, on_write);
    ASSERT(ret_code == NANOEV_SUCCESS);
    
    nanoev_loop_run(loop);

    nanoev_event_free(udp);

    nanoev_loop_free(loop);

    nanoev_term();

    return 0;
}

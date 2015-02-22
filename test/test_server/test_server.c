#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include "nanoev.h"
#include <assert.h>
#define ASSERT assert

/*----------------------------------------------------------------------------*/

static void on_accept(
    nanoev_event *tcp, 
    int status,
    nanoev_event *tcp_new
    );

/*----------------------------------------------------------------------------*/

int main(int argc, char* argv[])
{
    int ret_code;
    nanoev_loop *loop;
    nanoev_event *tcp;

    ret_code = nanoev_init();
    ASSERT(ret_code == NANOEV_SUCCESS);
    
    loop = nanoev_loop_new(NULL);
    ASSERT(loop);

    tcp = nanoev_event_new(nanoev_event_tcp, loop, NULL);
    ASSERT(tcp);
    ret_code = nanoev_tcp_listen(tcp, "127.0.0.1", 4000, 5);
    ASSERT(ret_code == NANOEV_SUCCESS);
    ret_code = nanoev_tcp_accept(tcp, on_accept, NULL);
    ASSERT(ret_code == NANOEV_SUCCESS);

    ret_code = nanoev_loop_run(loop);
    ASSERT(ret_code == NANOEV_SUCCESS);
    
    nanoev_event_free(tcp);
    
    nanoev_loop_free(loop);
    
    nanoev_term();

    return 0;
}

/*----------------------------------------------------------------------------*/

static void on_accept(
    nanoev_event *tcp, 
    int status,
    nanoev_event *tcp_new
    )
{
    if (status) {
        printf("on_accept status = %d\n", status);
        return;
    }

    ASSERT(tcp_new);
    nanoev_event_free(tcp_new);
}

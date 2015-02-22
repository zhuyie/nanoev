#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include "nanoev.h"
#include <assert.h>
#define ASSERT assert

/*----------------------------------------------------------------------------*/

typedef enum {
    client_state_init = 0,
    client_state_send,
    client_state_recv,
} client_state;

typedef struct {
    client_state state;
    unsigned int transfered;

    unsigned char *out_buf;
    unsigned int out_buf_capacity;
    unsigned int out_buf_size;

    unsigned char *in_buf;
    unsigned int in_buf_capacity;
    unsigned int in_buf_size;
} client;

static client* client_new();
static void client_free(client *c);

static void on_accept(
    nanoev_event *tcp, 
    int status,
    nanoev_event *tcp_new
    );
static void* alloc_userdata(
    int alloc,
    void *p
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
    ret_code = nanoev_tcp_accept(tcp, on_accept, alloc_userdata);
    ASSERT(ret_code == NANOEV_SUCCESS);

    ret_code = nanoev_loop_run(loop);
    ASSERT(ret_code == NANOEV_SUCCESS);
    
    nanoev_event_free(tcp);
    
    nanoev_loop_free(loop);
    
    nanoev_term();

    return 0;
}

/*----------------------------------------------------------------------------*/

static client* client_new()
{
    client *c = (client*)malloc(sizeof(client));
    if (c) {
        c->state = client_state_init;
        c->transfered = 0;
        c->out_buf = NULL;
        c->out_buf_capacity = 0;
        c->out_buf_size = 0;
        c->in_buf = NULL;
        c->in_buf_capacity = 0;
        c->in_buf_size = 0;
    }
    return c;
}

static void client_free(client *c)
{
    free(c->out_buf);
    free(c->in_buf);
    free(c);
}

static void on_accept(
    nanoev_event *tcp, 
    int status,
    nanoev_event *tcp_new
    )
{
    char ip[16];
    unsigned short port;
    client *c;
    int ret_code;

    if (status) {
        printf("on_accept status = %d\n", status);
        return;
    }

    ASSERT(tcp_new);
    c = (client*)nanoev_event_userdata(tcp_new);
    ASSERT(c);

    ret_code = nanoev_tcp_addr(tcp_new, 0, ip, &port);
    if (ret_code != NANOEV_SUCCESS) {
        printf("nanoev_tcp_addr failed, code = %u\n", ret_code);
        nanoev_event_free(tcp_new);
        client_free(c);
        return;
    }
    printf("Client %s:%d connected\n", ip, (int)port);

    nanoev_event_free(tcp_new);
    client_free(c);

    /* 继续accept下一个client */
    ret_code = nanoev_tcp_accept(tcp, on_accept, alloc_userdata);
    if (ret_code != NANOEV_SUCCESS) {
        printf("nanoev_tcp_accept failed, code = %u\n", ret_code);
        return;
    }
}

static void* alloc_userdata(
    int alloc,
    void *p
    )
{
    if (alloc) {
        return client_new();
    } else {
        client_free((client*)p);
        return NULL;
    }
}

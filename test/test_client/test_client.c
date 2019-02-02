#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include "nanoev.h"
#include <assert.h>
#define ASSERT assert

/*----------------------------------------------------------------------------*/

const char *out_msg = "Hello, World!";
static int times = 5;

typedef enum {
    client_state_init = 0,
    client_state_send,
    client_state_recv,
} client_state;

typedef struct {
    client_state state;

    unsigned char *out_buf;
    unsigned int out_buf_capacity;
    unsigned int out_buf_size;
    unsigned int out_buf_sent;

    unsigned char *in_buf;
    unsigned int in_buf_capacity;
    unsigned int in_buf_size;
} client;

static client* client_new();
static void client_free(client *c);
static int ensure_in_buf(client *c, unsigned int capacity);
static int get_remain_size(client *c);
static int write_to_buf(client *c, const char *msg);

static void on_connect(
    nanoev_event *tcp, 
    int status
    );
static void on_write(
    nanoev_event *tcp, 
    int status, 
    void *buf, 
    unsigned int bytes
    );
static void on_read(
    nanoev_event *tcp, 
    int status, 
    void *buf, 
    unsigned int bytes
    );

/*----------------------------------------------------------------------------*/

int main(int argc, char* argv[])
{
    int ret_code;
    nanoev_loop *loop;
    nanoev_event *tcp;
    struct nanoev_addr server_addr;
    client *c;

    ret_code = nanoev_init();
    ASSERT(ret_code == NANOEV_SUCCESS);
    
    loop = nanoev_loop_new(NULL);
    ASSERT(loop);

    c = client_new();
    ASSERT(c);

    tcp = nanoev_event_new(nanoev_event_tcp, loop, c);
    ASSERT(tcp);
    nanoev_addr_init(&server_addr, "127.0.0.1", 4000);
    ret_code = nanoev_tcp_connect(tcp, &server_addr, on_connect);
    ASSERT(ret_code == NANOEV_SUCCESS);

    ret_code = nanoev_loop_run(loop);
    ASSERT(ret_code == NANOEV_SUCCESS);
    
    nanoev_event_free(tcp);
    
    client_free(c);
    
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
        c->out_buf = NULL;
        c->out_buf_capacity = 0;
        c->out_buf_size = 0;
        c->out_buf_sent = 0;
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

static int ensure_in_buf(client *c, unsigned int capacity)
{
    void *new_buf;
    if (c->in_buf_capacity < capacity) {
        new_buf = realloc(c->in_buf, capacity);
        if (!new_buf)
            return -1;
        c->in_buf = (unsigned char*)new_buf;
        c->in_buf_capacity = capacity;
    }
    return 0;
}

static int get_remain_size(client *c)
{
    unsigned int total;
    if (c->in_buf_size < sizeof(unsigned int)) {
        total = sizeof(unsigned int);
    } else {
        total = sizeof(unsigned int) + *((unsigned int*)c->in_buf);
    }
    return total - c->in_buf_size;
}

static int write_to_buf(client *c, const char *msg)
{
    unsigned int len = (unsigned int)strlen(msg) + 1;

    unsigned int required_cb = sizeof(unsigned int) + len;
    if (c->out_buf_capacity < required_cb) {
        void *new_buf = realloc(c->out_buf, required_cb);
        if (!new_buf)
            return -1;
        c->out_buf = (unsigned char *)new_buf;
        c->out_buf_capacity = required_cb;
    }

    *((unsigned int*)c->out_buf) = len;
    c->out_buf_size = sizeof(unsigned int);

    memcpy(c->out_buf + c->out_buf_size, msg, len);
    c->out_buf_size += len;

    return 0;
}

static void on_connect(
    nanoev_event *tcp, 
    int status
    )
{
    client *c;
    int ret_code;

    if (status) {
        printf("on_connect status = %d\n", status);
        return;
    }
    
    c = (client*)nanoev_event_userdata(tcp);
    ASSERT(c->state == client_state_init);

    ret_code = write_to_buf(c, out_msg);
    if (ret_code != 0) {
        printf("write_to_buf failed\n");
        return;
    }
    ret_code = nanoev_tcp_write(tcp, c->out_buf, c->out_buf_size, on_write);
    if (ret_code != NANOEV_SUCCESS) {
        printf("nanoev_tcp_write failed, code = %d\n", ret_code);
        return;
    }

    c->state = client_state_send;
}

static void on_write(
    nanoev_event *tcp, 
    int status, 
    void *buf, 
    unsigned int bytes
    )
{
    client *c;
    int ret_code;

    if (status) {
        printf("on_write status = %d\n", status);
        return;
    }

    ASSERT(bytes);
    c = (client*)nanoev_event_userdata(tcp);
    ASSERT(c->state == client_state_send);

    c->out_buf_sent += bytes;

    if (c->out_buf_sent < c->out_buf_size) {
        ret_code = nanoev_tcp_write(tcp, c->out_buf + c->out_buf_sent, c->out_buf_size - c->out_buf_sent, on_write);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_tcp_write failed, code = %d\n", ret_code);
            return;
        }

    } else {
        c->in_buf_size = 0;
        ret_code = ensure_in_buf(c, 100);
        if (ret_code != 0) {
            printf("ensure_in_buf failed\n");
            return;
        }
        ret_code = nanoev_tcp_read(tcp, c->in_buf, c->in_buf_capacity, on_read);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_tcp_read failed, code = %d\n", ret_code);
            return;
        }

        c->state = client_state_recv;
    }
}

static void on_read(
    nanoev_event *tcp, 
    int status, 
    void *buf, 
    unsigned int bytes
    )
{
    client *c;
    int ret_code;

    if (status) {
        printf("on_read status = %d\n", status);
        return;
    }

    if (!bytes) {
        printf("Server close the connection\n");
        return;
    }

    c = (client*)nanoev_event_userdata(tcp);
    ASSERT(c->state == client_state_recv);

    c->in_buf_size += bytes;

    bytes = get_remain_size(c);
    if (bytes > 0) {
        ret_code = ensure_in_buf(c, c->in_buf_size + bytes);
        if (ret_code != 0) {
            printf("ensure_in_buf failed\n");
            return;
        }
        ret_code = nanoev_tcp_read(tcp, c->in_buf + c->in_buf_size, bytes, on_read);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_tcp_read failed, code = %d\n", ret_code);
            return;
        }

    } else {
        ASSERT(c->in_buf);
        ASSERT(c->in_buf_size);
        printf("Server return %u bytes : %s\n", c->in_buf_size, c->in_buf + sizeof(unsigned int));
        
        --times;
        if (times > 0) {
            c->out_buf_sent = 0;
            ret_code = write_to_buf(c, out_msg);
            if (ret_code != 0) {
                printf("write_to_buf failed\n");
                return;
            }
            ret_code = nanoev_tcp_write(tcp, c->out_buf, c->out_buf_size, on_write);
            if (ret_code != NANOEV_SUCCESS) {
                printf("nanoev_tcp_write failed, code = %d\n", ret_code);
                return;
            }

            c->state = client_state_send;
        }
    }
}

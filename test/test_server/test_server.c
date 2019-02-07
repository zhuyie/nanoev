#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include "nanoev.h"
#include <assert.h>
#define ASSERT assert

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#else
# include <signal.h>
#endif

/*----------------------------------------------------------------------------*/

#define TIMEOUT_SECONDS 30

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

    nanoev_event *tcp;
    nanoev_event *timeout_timer;
} client;

static client* client_new(nanoev_loop *loop);
static void client_free(client *c);
static int ensure_in_buf(client *c, unsigned int capacity);
static int get_remain_size(client *c);
static int write_to_buf(client *c, const unsigned char *msg);

static void on_accept(
    nanoev_event *tcp, 
    int status,
    nanoev_event *tcp_new
    );
static void* alloc_userdata(
    void *context,
    void *userdata
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
static void on_async(
    nanoev_event *async
    );
static void on_timer(
    nanoev_event *timer
    );

/*----------------------------------------------------------------------------*/

static nanoev_event *async_for_ctrl_c;
#ifdef _WIN32
static BOOL WINAPI CtrlCHandler(DWORD dwCtrlType)
{
    int ret;
    ASSERT(async_for_ctrl_c);
    ret = nanoev_async_send(async_for_ctrl_c);
    ASSERT(ret == NANOEV_SUCCESS);
    return TRUE;
}
#else
static void sigint_handler(int sig)
{
    int ret;
    ASSERT(async_for_ctrl_c);
    ret = nanoev_async_send(async_for_ctrl_c);
    ASSERT(ret == NANOEV_SUCCESS);
}
#endif

typedef struct {
    nanoev_loop *loop;
} global_data;

int main(int argc, char* argv[])
{
    int ret_code;
    global_data global;
    int family;
    const char *addr;
    unsigned short port;
    nanoev_loop *loop;
    nanoev_event *tcp;
    struct nanoev_addr local_addr;
    nanoev_event *async;

    if (argc > 1 && strcmp(argv[1], "-ipv6") == 0) {
        family = NANOEV_AF_INET6;
        addr = "::1";
        port = 4000;
    } else {
        family = NANOEV_AF_INET;
        addr = "127.0.0.1";
        port = 4000;
    }

    ret_code = nanoev_init();
    ASSERT(ret_code == NANOEV_SUCCESS);
    
    loop = nanoev_loop_new(NULL);
    ASSERT(loop);

    global.loop = loop;

    tcp = nanoev_event_new(nanoev_event_tcp, loop, &global);
    ASSERT(tcp);
    async = nanoev_event_new(nanoev_event_async, loop, NULL);
    ASSERT(async);

    nanoev_addr_init(&local_addr, family, addr, port);
    ret_code = nanoev_tcp_listen(tcp, &local_addr, 5);
    ASSERT(ret_code == NANOEV_SUCCESS);
    ret_code = nanoev_tcp_accept(tcp, on_accept, alloc_userdata);
    ASSERT(ret_code == NANOEV_SUCCESS);
    printf("Listening at %s:%d\n", addr, (int)port);

    nanoev_async_start(async, on_async);
    async_for_ctrl_c = async;
#ifdef _WIN32
    SetConsoleCtrlHandler(CtrlCHandler, TRUE);
#else
    signal(SIGINT, sigint_handler);
#endif
    printf("Press Ctrl+C to break...\n");

    ret_code = nanoev_loop_run(loop);
    ASSERT(ret_code == NANOEV_SUCCESS);
    
    nanoev_event_free(tcp);
    nanoev_event_free(async);

    nanoev_loop_free(loop);
    
    nanoev_term();

    return 0;
}

/*----------------------------------------------------------------------------*/

static client* client_new(nanoev_loop *loop)
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
        c->tcp = NULL;
        c->timeout_timer = nanoev_event_new(nanoev_event_timer, loop, c);
        ASSERT(c->timeout_timer);
    }
    return c;
}

static void client_free(client *c)
{
    free(c->out_buf);
    free(c->in_buf);
    nanoev_event_free(c->timeout_timer);
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

static int write_to_buf(client *c, const unsigned char *msg)
{
    unsigned int len = (unsigned int)strlen((const char*)msg) + 1;

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

static void on_accept(
    nanoev_event *tcp, 
    int status,
    nanoev_event *tcp_new
    )
{
    struct nanoev_addr addr;
    char ip[46];
    unsigned short port;
    client *c;
    nanoev_timeval after; 
    int ret_code;

    if (status) {
        printf("on_accept status = %d\n", status);
        return;
    }

    ASSERT(tcp_new);
    c = (client*)nanoev_event_userdata(tcp_new);
    ASSERT(c);

    c->tcp = tcp_new;

    ret_code = nanoev_tcp_addr(tcp_new, 0, &addr);
    if (ret_code != NANOEV_SUCCESS) {
        printf("nanoev_tcp_addr failed, code = %u\n", ret_code);
        goto ON_ACCEPT_ERROR;
    }
    nanoev_addr_get_ip(&addr, ip, sizeof(ip));
    nanoev_addr_get_port(&addr, &port);
    printf("Client %s:%d connected\n", ip, (int)port);

    ret_code = ensure_in_buf(c, 100);
    if (ret_code) {
        printf("ensure_in_buf failed\n");
        goto ON_ACCEPT_ERROR;
    }
    ret_code = nanoev_tcp_read(tcp_new, c->in_buf, c->in_buf_capacity, on_read);
    if (ret_code != NANOEV_SUCCESS) {
        printf("nanoev_tcp_read failed, code = %u\n", ret_code);
        goto ON_ACCEPT_ERROR;
    }
    after.tv_sec = TIMEOUT_SECONDS;
    after.tv_usec = 0;
    ret_code = nanoev_timer_add(c->timeout_timer, after, 0, on_timer);
    if (ret_code != NANOEV_SUCCESS) {
        printf("nanoev_timer_add failed, code = %u\n", ret_code);
        goto ON_ACCEPT_ERROR;
    }

    c->state = client_state_recv;

ON_ACCEPT_ERROR:
    if (ret_code) {
        nanoev_event_free(tcp_new);
        client_free(c);
    }

    ret_code = nanoev_tcp_accept(tcp, on_accept, alloc_userdata);
    if (ret_code != NANOEV_SUCCESS) {
        printf("nanoev_tcp_accept failed, code = %u\n", ret_code);
        return;
    }
}

static void* alloc_userdata(
    void *context,
    void *userdata
    )
{
    global_data *global = (global_data*)context;
    ASSERT(global);

    if (!userdata) {
        return client_new(global->loop);
    } else {
        client_free((client*)userdata);
        return NULL;
    }
}

static void on_write(
    nanoev_event *tcp, 
    int status, 
    void *buf, 
    unsigned int bytes
    )
{
    client *c;
    nanoev_timeval after;
    int ret_code;

    c = (client*)nanoev_event_userdata(tcp);
    ASSERT(c->state == client_state_send);

    nanoev_timer_del(c->timeout_timer);

    if (status) {
        printf("on_read status = %d\n", status);
        goto ERROR_EXIT;
    }

    ASSERT(bytes);
    c->out_buf_sent += bytes;

    if (c->out_buf_sent < c->out_buf_size) {
        ret_code = nanoev_tcp_write(tcp, c->out_buf + c->out_buf_sent, c->out_buf_size - c->out_buf_sent, on_write);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_tcp_write failed, code = %d\n", ret_code);
            goto ERROR_EXIT;
        }
        after.tv_sec = TIMEOUT_SECONDS;
        after.tv_usec = 0;
        ret_code = nanoev_timer_add(c->timeout_timer, after, 0, on_timer);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_timer_add failed, code = %u\n", ret_code);
            goto ERROR_EXIT;
        }

    } else {
        c->in_buf_size = 0;
        c->out_buf_size = 0;
        c->out_buf_sent = 0;

        ret_code = nanoev_tcp_read(tcp, c->in_buf, c->in_buf_capacity, on_read);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_tcp_read failed, code = %u\n", ret_code);
            goto ERROR_EXIT;
        }
        after.tv_sec = TIMEOUT_SECONDS;
        after.tv_usec = 0;
        ret_code = nanoev_timer_add(c->timeout_timer, after, 0, on_timer);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_timer_add failed, code = %u\n", ret_code);
            goto ERROR_EXIT;
        }

        c->state = client_state_recv;
    }

    return;

ERROR_EXIT:
    nanoev_event_free(tcp);
    client_free(c);
}

static void on_read(
    nanoev_event *tcp, 
    int status, 
    void *buf, 
    unsigned int bytes
    )
{
    client *c;
    nanoev_timeval after;
    int ret_code;

    c = (client*)nanoev_event_userdata(tcp);
    ASSERT(c->state == client_state_recv);

    nanoev_timer_del(c->timeout_timer);

    if (status) {
        printf("on_read status = %d\n", status);
        goto ERROR_EXIT;
    }

    if (!bytes) {
        printf("Client disconnected\n");
        goto ERROR_EXIT;
    }

    ASSERT(bytes);
    c->in_buf_size += bytes;

    bytes = get_remain_size(c);
    if (bytes > 0) {
        ret_code = ensure_in_buf(c, c->in_buf_size + bytes);
        if (ret_code != 0) {
            printf("ensure_buf failed\n");
            goto ERROR_EXIT;
        }
        ret_code = nanoev_tcp_read(tcp, c->in_buf + c->in_buf_size, bytes, on_read);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_tcp_read failed, code = %d\n", ret_code);
            goto ERROR_EXIT;
        }
        after.tv_sec = TIMEOUT_SECONDS;
        after.tv_usec = 0;
        ret_code = nanoev_timer_add(c->timeout_timer, after, 0, on_timer);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_timer_add failed, code = %u\n", ret_code);
            goto ERROR_EXIT;
        }

    } else {
        ASSERT(c->in_buf);
        ASSERT(c->in_buf_size);
        ret_code = write_to_buf(c, c->in_buf + sizeof(unsigned int));
        if (ret_code != 0) {
            printf("write_to_buf failed\n");
            return;
        }

        c->out_buf_sent = 0;
        ret_code = nanoev_tcp_write(tcp, c->out_buf, c->out_buf_size, on_write);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_tcp_write failed, code = %d\n", ret_code);
            return;
        }
        after.tv_sec = TIMEOUT_SECONDS;
        after.tv_usec = 0;
        ret_code = nanoev_timer_add(c->timeout_timer, after, 0, on_timer);
        if (ret_code != NANOEV_SUCCESS) {
            printf("nanoev_timer_add failed, code = %u\n", ret_code);
            goto ERROR_EXIT;
        }

        c->state = client_state_send;
    }

    return;

ERROR_EXIT:
    nanoev_event_free(tcp);
    client_free(c);
}

static void on_async(
    nanoev_event *async
    )
{
    nanoev_loop *loop = nanoev_event_loop(async);
    printf("Bye\n");
    nanoev_loop_break(loop);
}

static void on_timer(
    nanoev_event *timer
    )
{
    client *c = (client*)nanoev_event_userdata(timer);
    printf("TCP Reading/Writing Timeout\n");
    nanoev_event_free(c->tcp);
    client_free(c);
}

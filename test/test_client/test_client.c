#include <stdio.h>
#include <memory.h>
#include <string.h>
#include "nanoev.h"
#include <assert.h>
#define ASSERT assert

/*----------------------------------------------------------------------------*/

const char *out_msg = "Hello, Server!";

typedef enum {
    client_state_connect,
    client_state_send_hdr,
    client_state_send_body,
    client_state_recv_hdr,
    client_state_recv_body,
} client_state;

typedef struct {
    client_state state;
    unsigned int len;
    unsigned int transfered;
    unsigned char out_buf[100];
    unsigned char in_buf[100];
} client;

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
    client c;

    ret_code = nanoev_init();
    ASSERT(ret_code == NANOEV_SUCCESS);

    loop = nanoev_loop_new(NULL);
    ASSERT(loop);

    memset(&c, 0, sizeof(c));
    c.state = client_state_connect;
    c.len = 0;
    c.transfered = 0;

    tcp = nanoev_event_new(nanoev_event_tcp, loop, &c);
    ASSERT(tcp);
    ret_code = nanoev_tcp_connect(tcp, "127.0.0.1", 4000, on_connect);
    ASSERT(ret_code == NANOEV_SUCCESS);

    ret_code = nanoev_loop_run(loop);
    ASSERT(ret_code == NANOEV_SUCCESS);
    
    nanoev_event_free(tcp);
    nanoev_loop_free(loop);
    nanoev_term();

    return 0;
}

/*----------------------------------------------------------------------------*/

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
    ASSERT(c->state == client_state_connect);
    
    c->len = (unsigned int)strlen(out_msg) + 1;
    memcpy(c->out_buf, out_msg, c->len);
    c->transfered = 0;

    ret_code = nanoev_tcp_write(tcp, &c->len, sizeof(c->len), on_write);
    if (ret_code != NANOEV_SUCCESS) {
        printf("nanoev_tcp_write failed, code = %d\n", ret_code);
        return;
    }
    c->state = client_state_send_hdr;
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

    c->transfered += bytes;

    if (c->state == client_state_send_hdr) {

        if (c->transfered < sizeof(c->len)) {
            /* 继续发送剩余的hdr */
            ret_code = nanoev_tcp_write(tcp, (unsigned char*)(&(c->len)) + c->transfered, sizeof(c->len) - c->transfered, on_write);
            if (ret_code != NANOEV_SUCCESS) {
                printf("nanoev_tcp_write failed, code = %d\n", ret_code);
                return;
            }
        } else {
            /* 开始发送body */
            c->state = client_state_send_body;
            c->transfered = 0;
            ret_code = nanoev_tcp_write(tcp, c->out_buf, c->len, on_write);
            if (ret_code != NANOEV_SUCCESS) {
                printf("nanoev_tcp_write failed, code = %d\n", ret_code);
                return;
            }
        }

    } else if (c->state == client_state_send_body) {

        if (c->transfered < c->len) {
            /* 继续发送剩余的数据 */
            ret_code = nanoev_tcp_write(tcp, c->out_buf + c->transfered, c->len - c->transfered, on_write);
            if (ret_code != NANOEV_SUCCESS) {
                printf("nanoev_tcp_write failed, code = %d\n", ret_code);
                return;
            }

        } else {
            /* 开始接收响应头部 */
            c->len = 0;
            c->transfered = 0;
            c->state = client_state_recv_hdr;
            ret_code = nanoev_tcp_read(tcp, &c->len, sizeof(c->len), on_read);
            if (ret_code != NANOEV_SUCCESS) {
                printf("nanoev_tcp_read failed, code = %d\n", ret_code);
                return;
            }
        }

    } else {
        ASSERT(0);
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

    c->transfered += bytes;

    if (c->state == client_state_recv_hdr) {

        if (c->transfered < sizeof(c->len)) {
            /* 继续接收剩余的hdr */
            ret_code = nanoev_tcp_read(tcp, (unsigned char*)(&c->len) + c->transfered, sizeof(c->len) - c->transfered, on_read);
            if (ret_code != NANOEV_SUCCESS) {
                printf("nanoev_tcp_read failed, code = %d\n", ret_code);
                return;
            }

        } else {
            /* 接收hdr完成 */
            c->transfered = 0;
            c->state = client_state_recv_body;
            ret_code = nanoev_tcp_read(tcp, c->in_buf, c->len, on_read);
            if (ret_code != NANOEV_SUCCESS) {
                printf("nanoev_tcp_read failed, code = %d\n", ret_code);
                return;
            }
        }
    
    } else if (c->state == client_state_recv_body) {

        if (c->transfered < c->len) {
            /* 继续接收剩余的数据 */
            ret_code = nanoev_tcp_read(tcp, c->in_buf + c->transfered, c->len - c->transfered, on_read);
            if (ret_code != NANOEV_SUCCESS) {
                printf("nanoev_tcp_read failed, code = %d\n", ret_code);
                return;
            }

        } else {
            /* 接收完成 */
            printf("Server return %u bytes : %s\n", c->len, c->in_buf);
        }

    } else {
        ASSERT(0);
    }
}

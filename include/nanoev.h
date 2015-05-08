#ifndef __NANOEV_H__
#define __NANOEV_H__

/*----------------------------------------------------------------------------*/

/* return codes */
#define NANOEV_SUCCESS             0
#define NANOEV_ERROR_INVALID_ARG   1
#define NANOEV_ERROR_ACCESS_DENIED 2
#define NANOEV_ERROR_OUT_OF_MEMORY 3
#define NANOEV_ERROR_FAIL          4

/*----------------------------------------------------------------------------*/

int nanoev_init();

void nanoev_term();

/*----------------------------------------------------------------------------*/

struct nanoev_loop;
typedef struct nanoev_loop nanoev_loop;
struct nanoev_timeval;

nanoev_loop* nanoev_loop_new(
    void *userdata
    );

void nanoev_loop_free(
    nanoev_loop *loop
    );

int nanoev_loop_run(
    nanoev_loop *loop
    );

void nanoev_loop_break(
    nanoev_loop *loop
    );

void* nanoev_loop_userdata();

void nanoev_loop_now(
    nanoev_loop *loop,
    struct nanoev_timeval *now
    );

/*----------------------------------------------------------------------------*/

struct nanoev_event;
typedef struct nanoev_event nanoev_event;

typedef enum {
    nanoev_event_unknown = 0,
    nanoev_event_tcp,
    nanoev_event_udp,
    nanoev_event_async,
    nanoev_event_timer,
} nanoev_event_type;

nanoev_event* nanoev_event_new(
    nanoev_event_type type, 
    nanoev_loop *loop, 
    void *userdata
    );

void nanoev_event_free(
    nanoev_event *event
    );

nanoev_event_type nanoev_event__type(
    nanoev_event *event
    );

nanoev_loop* nanoev_event_loop(
    nanoev_event *event
    );

void* nanoev_event_userdata(
    nanoev_event *event
    );

/*----------------------------------------------------------------------------*/

struct nanoev_addr {
    unsigned int ip;
    unsigned short port;
};

void nanoev_addr_init(
    struct nanoev_addr *addr, 
    const char *ip, 
    unsigned short port
    );

void nanoev_addr_get_ip(
    const struct nanoev_addr *addr, 
    char ip[16]
    );

void nanoev_addr_get_port(
    const struct nanoev_addr *addr, 
    unsigned short *port
    );

/*----------------------------------------------------------------------------*/

typedef void (*nanoev_tcp_on_connect)(
    nanoev_event *tcp, 
    int status
    );
typedef void (*nanoev_tcp_on_accept)(
    nanoev_event *tcp, 
    int status,
    nanoev_event *tcp_new
    );
typedef void* (*nanoev_tcp_alloc_userdata)(
    void *context,
    void *userdata
    );
typedef void (*nanoev_tcp_on_write)(
    nanoev_event *tcp, 
    int status, 
    void *buf, 
    unsigned int bytes
    );
typedef void (*nanoev_tcp_on_read)(
    nanoev_event *tcp, 
    int status, 
    void *buf, 
    unsigned int bytes
    );

int nanoev_tcp_connect(
    nanoev_event *event, 
    const struct nanoev_addr *server_addr,
    nanoev_tcp_on_connect callback
    );

int nanoev_tcp_listen(
    nanoev_event *event, 
    const struct nanoev_addr *local_addr,
    int backlog
    );

int nanoev_tcp_accept(
    nanoev_event *event, 
    nanoev_tcp_on_accept callback,
    nanoev_tcp_alloc_userdata alloc_userdata
    );

int nanoev_tcp_write(
    nanoev_event *event, 
    const void *buf, 
    unsigned int len,
    nanoev_tcp_on_write callback
    );

int nanoev_tcp_read(
    nanoev_event *event, 
    void *buf, 
    unsigned int len,
    nanoev_tcp_on_read callback
    );

int nanoev_tcp_addr(
    nanoev_event *event, 
    int local,
    struct nanoev_addr *addr
    );

int nanoev_tcp_error(
    nanoev_event *event
    );

/*----------------------------------------------------------------------------*/

typedef void (*nanoev_udp_on_read)(
    nanoev_event *udp,
    int status,
    void *buf,
    unsigned int bytes,
    const struct nanoev_addr *from_addr
    );
typedef void (*nanoev_udp_on_write)(
    nanoev_event *udp,
    int status,
    void *buf,
    unsigned int bytes
    );

int nanoev_udp_read(
    nanoev_event *event, 
    const void *buf, 
    unsigned int len, 
    nanoev_udp_on_read callback
    );

int nanoev_udp_write(
    nanoev_event *event, 
    const void *buf, 
    unsigned int len, 
    const struct nanoev_addr *to_addr,
    nanoev_udp_on_write callback
    );

int nanoev_udp_bind(
    nanoev_event *event,
    const struct nanoev_addr *addr
    );

int nanoev_udp_error(
    nanoev_event *event
    );

/*----------------------------------------------------------------------------*/

typedef void (*nanoev_async_callback)(
    nanoev_event *async
    );

void nanoev_async_start(
    nanoev_event *event,
    nanoev_async_callback callback
    );

void nanoev_async_send(
    nanoev_event *event
    );

int nanoev_async_pending(
    nanoev_event *event
    );

/*----------------------------------------------------------------------------*/

struct nanoev_timeval {
    unsigned int tv_sec;     /* seconds */
    unsigned int tv_usec;    /* microseconds */
};

void nanoev_now(
    struct nanoev_timeval *now
    );

typedef void (*nanoev_timer_callback)(
    nanoev_event *timer
    );

int nanoev_timer_add(
    nanoev_event *event,
    struct nanoev_timeval after,
    int repeat,
    nanoev_timer_callback callback
    );

int nanoev_timer_del(
    nanoev_event *event
    );

/*----------------------------------------------------------------------------*/

#endif  /* __NANOEV_H__ */

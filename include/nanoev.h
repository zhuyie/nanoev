#ifndef __NANOEV_H__
#define __NANOEV_H__

#ifdef _WIN32
#  define _CRT_SECURE_NO_WARNINGS
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <winsock2.h>
#else
#  include <sys/socket.h>
#  include <time.h>
#endif

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

typedef struct timeval nanoev_timeval;

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
    nanoev_timeval *now
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

void nanoev_event_set_userdata(
    nanoev_event *event,
    void *userdata
    );

/*----------------------------------------------------------------------------*/

#define nanoev_addr sockaddr_storage
#define NANOEV_AF_INET    AF_INET
#define NANOEV_AF_INET6   AF_INET6

int nanoev_addr_init(
    struct nanoev_addr *addr, 
    int family,
    const char *ip, 
    unsigned short port
    );

int nanoev_addr_get_ip(
    const struct nanoev_addr *addr, 
    char *ip,
    int ip_len
    );

int nanoev_addr_get_port(
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

int nanoev_tcp_setopt(
    nanoev_event *event,
    int level,
    int optname,
    const char *optval,
    int optlen
    );

int nanoev_tcp_getopt(
    nanoev_event *event,
    int level,
    int optname,
    char *optval,
    int *optlen
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
    void *buf, 
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

int nanoev_udp_setopt(
    nanoev_event *event,
    int level,
    int optname,
    const char *optval,
    int optlen
    );

int nanoev_udp_getopt(
    nanoev_event *event,
    int level,
    int optname,
    char *optval,
    int *optlen
    );

/*----------------------------------------------------------------------------*/

typedef void (*nanoev_async_callback)(
    nanoev_event *async
    );

int nanoev_async_start(
    nanoev_event *event,
    nanoev_async_callback callback
    );

int nanoev_async_send(
    nanoev_event *event
    );

/*----------------------------------------------------------------------------*/

void nanoev_now(
    nanoev_timeval *now
    );

typedef void (*nanoev_timer_callback)(
    nanoev_event *timer
    );

int nanoev_timer_add(
    nanoev_event *event,
    nanoev_timeval after,
    int repeat,
    nanoev_timer_callback callback
    );

int nanoev_timer_del(
    nanoev_event *event
    );

/*----------------------------------------------------------------------------*/

#endif  /* __NANOEV_H__ */

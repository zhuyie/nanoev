#ifndef __NANOEV_H__
#define __NANOEV_H__

#ifdef _WIN32
#  define _CRT_SECURE_NO_WARNINGS
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <winsock2.h>
#else
#  include <sys/socket.h>
#  include <sys/time.h>
#endif

/*----------------------------------------------------------------------------*/

/* return codes */
#define NANOEV_SUCCESS             0
#define NANOEV_ERROR_INVALID_ARG   1
#define NANOEV_ERROR_ACCESS_DENIED 2
#define NANOEV_ERROR_OUT_OF_MEMORY 3
#define NANOEV_ERROR_FAIL          4

/*----------------------------------------------------------------------------*/

/*
 * nanoev_init
 *   Initialize process-wide nanoev platform state.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   Call before creating loops or events. On Windows this initializes Winsock.
 */
int nanoev_init(void);

/*
 * nanoev_term
 *   Release process-wide platform state initialized by nanoev_init().
 */
void nanoev_term(void);

/*----------------------------------------------------------------------------*/

struct nanoev_loop;
typedef struct nanoev_loop nanoev_loop;

typedef struct timeval nanoev_timeval;

/*
 * nanoev_loop_new
 *   Create a new event loop.
 *
 * Parameters:
 *   userdata - User pointer stored on the loop.
 *
 * Returns:
 *   A loop pointer on success, or NULL on failure.
 */
nanoev_loop* nanoev_loop_new(
    void *userdata
    );

/*
 * nanoev_loop_free
 *   Free an event loop.
 *
 * Parameters:
 *   loop - Loop to free.
 *
 * Notes:
 *   Events created on the loop should be freed before freeing the loop.
 */
void nanoev_loop_free(
    nanoev_loop *loop
    );

/*
 * nanoev_loop_run
 *   Run the loop until nanoev_loop_break() is called or the poller fails.
 *
 * Parameters:
 *   loop - Loop to run.
 *
 * Returns:
 *   NANOEV_SUCCESS on normal exit, otherwise NANOEV_ERROR_FAIL.
 */
int nanoev_loop_run(
    nanoev_loop *loop
    );

/*
 * nanoev_loop_break
 *   Request loop shutdown.
 *
 * Parameters:
 *   loop - Loop to stop.
 *
 * Notes:
 *   May be used to wake a running loop from another thread.
 */
void nanoev_loop_break(
    nanoev_loop *loop
    );

/*
 * nanoev_loop_userdata
 *   Return the userdata pointer passed to nanoev_loop_new().
 */
void* nanoev_loop_userdata(
    nanoev_loop *loop
    );

/*
 * nanoev_loop_now
 *   Return the loop's cached current time.
 *
 * Parameters:
 *   loop - Loop to query.
 *   now  - Output time value.
 *
 * Notes:
 *   Before the loop runs, this returns the current system time.
 */
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
    nanoev_event_dns,
} nanoev_event_type;

/*
 * nanoev_event_new
 *   Create an event of the requested type on a loop.
 *
 * Parameters:
 *   type     - Event type to create.
 *   loop     - Owning loop.
 *   userdata - User pointer stored on the event.
 *
 * Returns:
 *   An event pointer on success, or NULL on failure.
 *
 * Notes:
 *   Events belong to their loop and should be operated from the loop thread,
 *   except nanoev_async_send().
 */
nanoev_event* nanoev_event_new(
    nanoev_event_type type, 
    nanoev_loop *loop, 
    void *userdata
    );

/*
 * nanoev_event_free
 *   Free an event.
 *
 * Parameters:
 *   event - Event to free.
 *
 * Notes:
 *   Events may be freed from their own callbacks. Pending I/O events are
 *   released after their outstanding operation has completed.
 */
void nanoev_event_free(
    nanoev_event *event
    );

/*
 * nanoev_event_typeof
 *   Return the concrete type of an event.
 */
nanoev_event_type nanoev_event_typeof(
    nanoev_event *event
    );

/*
 * nanoev_event_loop
 *   Return the loop that owns an event.
 */
nanoev_loop* nanoev_event_loop(
    nanoev_event *event
    );

/*
 * nanoev_event_userdata
 *   Return the userdata pointer associated with an event.
 */
void* nanoev_event_userdata(
    nanoev_event *event
    );

/*
 * nanoev_event_set_userdata
 *   Replace the userdata pointer associated with an event.
 */
void nanoev_event_set_userdata(
    nanoev_event *event,
    void *userdata
    );

/*----------------------------------------------------------------------------*/

#define nanoev_addr sockaddr_storage
#define NANOEV_AF_UNSPEC  AF_UNSPEC
#define NANOEV_AF_INET    AF_INET
#define NANOEV_AF_INET6   AF_INET6

/*
 * nanoev_addr_init
 *   Initialize an address from a numeric IPv4 or IPv6 string and port.
 *
 * Parameters:
 *   addr   - Address object to initialize.
 *   family - NANOEV_AF_INET or NANOEV_AF_INET6.
 *   ip     - Numeric IP address string.
 *   port   - Host-byte-order port.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise NANOEV_ERROR_INVALID_ARG.
 */
int nanoev_addr_init(
    struct nanoev_addr *addr, 
    int family,
    const char *ip, 
    unsigned short port
    );

/*
 * nanoev_addr_get_ip
 *   Write the numeric IP string for an address into a buffer.
 *
 * Parameters:
 *   addr   - Address to query.
 *   ip     - Output buffer.
 *   ip_len - Output buffer length.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise NANOEV_ERROR_INVALID_ARG.
 */
int nanoev_addr_get_ip(
    const struct nanoev_addr *addr, 
    char *ip,
    int ip_len
    );

/*
 * nanoev_addr_get_port
 *   Return the host-byte-order port stored in an address.
 */
int nanoev_addr_get_port(
    const struct nanoev_addr *addr, 
    unsigned short *port
    );

/*----------------------------------------------------------------------------*/

/*
 * nanoev_dns_callback
 *   Callback invoked when a DNS resolution completes.
 *
 * Parameters:
 *   dns        - DNS event.
 *   status     - 0 on success, otherwise getaddrinfo or NANOEV_ERROR_* code.
 *   addrs      - Resolved addresses on success, valid only during callback.
 *   addr_count - Number of resolved addresses.
 */
typedef void (*nanoev_dns_callback)(
    nanoev_event *dns,
    int status,
    const struct nanoev_addr *addrs,
    unsigned int addr_count
    );

/*
 * nanoev_dns_resolve
 *   Resolve a host asynchronously using the system resolver.
 *
 * Parameters:
 *   event    - DNS event.
 *   host     - Hostname or numeric IP address.
 *   family   - NANOEV_AF_UNSPEC, NANOEV_AF_INET, or NANOEV_AF_INET6.
 *   port     - Host-byte-order port to store in each returned address.
 *   callback - Completion callback invoked on the event loop thread.
 *
 * Returns:
 *   NANOEV_SUCCESS if the operation was started, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   At most one resolve may be pending on an event at a time. Freeing the event
 *   while a resolve is pending cancels the callback, but the system resolver may
 *   continue blocking on a worker thread until it completes.
 */
int nanoev_dns_resolve(
    nanoev_event *event,
    const char *host,
    int family,
    unsigned short port,
    nanoev_dns_callback callback
    );

/*----------------------------------------------------------------------------*/

/*
 * nanoev_tcp_on_connect
 *   Callback invoked when a TCP connect operation completes.
 *
 * Parameters:
 *   tcp    - TCP event.
 *   status - 0 on success, otherwise a platform socket error.
 */
typedef void (*nanoev_tcp_on_connect)(
    nanoev_event *tcp, 
    int status
    );

/*
 * nanoev_tcp_on_accept
 *   Callback invoked when a TCP accept operation completes.
 *
 * Parameters:
 *   tcp     - Listening TCP event.
 *   status  - 0 on success, otherwise a platform socket error.
 *   tcp_new - Accepted TCP event on success, or NULL on failure.
 */
typedef void (*nanoev_tcp_on_accept)(
    nanoev_event *tcp, 
    int status,
    nanoev_event *tcp_new
    );

/*
 * nanoev_tcp_alloc_userdata
 *   Optional accepted-connection userdata allocator.
 *
 * Parameters:
 *   context  - Userdata from the listening TCP event.
 *   userdata - NULL to allocate, or a userdata pointer to release.
 *
 * Returns:
 *   On allocation, return userdata for the accepted TCP event. On release,
 *   return NULL.
 */
typedef void* (*nanoev_tcp_alloc_userdata)(
    void *context,
    void *userdata
    );

/*
 * nanoev_tcp_on_write
 *   Callback invoked when a TCP write operation completes.
 *
 * Parameters:
 *   tcp    - TCP event.
 *   status - 0 on success, otherwise a platform socket error.
 *   buf    - Buffer passed to nanoev_tcp_write().
 *   bytes  - Number of bytes written.
 *
 * Notes:
 *   bytes may be smaller than the requested length.
 */
typedef void (*nanoev_tcp_on_write)(
    nanoev_event *tcp, 
    int status, 
    void *buf, 
    unsigned int bytes
    );

/*
 * nanoev_tcp_on_read
 *   Callback invoked when a TCP read operation completes.
 *
 * Parameters:
 *   tcp    - TCP event.
 *   status - 0 on success, otherwise a platform socket error.
 *   buf    - Buffer passed to nanoev_tcp_read().
 *   bytes  - Number of bytes read.
 *
 * Notes:
 *   bytes may be smaller than the requested length. bytes == 0 means the peer
 *   closed the connection.
 */
typedef void (*nanoev_tcp_on_read)(
    nanoev_event *tcp, 
    int status, 
    void *buf, 
    unsigned int bytes
    );

#ifdef _WIN32
#  define NANOEV_TCP_SHUT_READ  SD_RECEIVE
#  define NANOEV_TCP_SHUT_WRITE SD_SEND
#  define NANOEV_TCP_SHUT_BOTH  SD_BOTH
#else
#  define NANOEV_TCP_SHUT_READ  SHUT_RD
#  define NANOEV_TCP_SHUT_WRITE SHUT_WR
#  define NANOEV_TCP_SHUT_BOTH  SHUT_RDWR
#endif

/*
 * nanoev_tcp_connect
 *   Start a non-blocking TCP connection.
 *
 * Parameters:
 *   event       - TCP event.
 *   server_addr - Remote address.
 *   callback    - Completion callback.
 *
 * Returns:
 *   NANOEV_SUCCESS if the operation was started, otherwise a NANOEV_ERROR_* code.
 */
int nanoev_tcp_connect(
    nanoev_event *event, 
    const struct nanoev_addr *server_addr,
    nanoev_tcp_on_connect callback
    );

/*
 * nanoev_tcp_listen
 *   Bind a TCP event to a local address and start listening.
 *
 * Parameters:
 *   event      - TCP event.
 *   local_addr - Local address.
 *   backlog    - Listen backlog.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 */
int nanoev_tcp_listen(
    nanoev_event *event, 
    const struct nanoev_addr *local_addr,
    int backlog
    );

/*
 * nanoev_tcp_accept
 *   Start one asynchronous accept operation on a listening TCP event.
 *
 * Parameters:
 *   event          - Listening TCP event.
 *   callback       - Completion callback.
 *   alloc_userdata - Optional userdata allocator for accepted events.
 *
 * Returns:
 *   NANOEV_SUCCESS if the operation was started, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   At most one accept may be pending on an event at a time.
 */
int nanoev_tcp_accept(
    nanoev_event *event, 
    nanoev_tcp_on_accept callback,
    nanoev_tcp_alloc_userdata alloc_userdata
    );

/*
 * nanoev_tcp_write
 *   Start one asynchronous TCP write operation.
 *
 * Parameters:
 *   event    - TCP event.
 *   buf      - Data buffer.
 *   len      - Number of bytes to write.
 *   callback - Completion callback.
 *
 * Returns:
 *   NANOEV_SUCCESS if the operation was started, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   buf must remain valid until callback runs. At most one write may be pending
 *   on an event at a time.
 */
int nanoev_tcp_write(
    nanoev_event *event, 
    const void *buf, 
    unsigned int len,
    nanoev_tcp_on_write callback
    );

/*
 * nanoev_tcp_read
 *   Start one asynchronous TCP read operation.
 *
 * Parameters:
 *   event    - TCP event.
 *   buf      - Receive buffer.
 *   len      - Maximum number of bytes to read.
 *   callback - Completion callback.
 *
 * Returns:
 *   NANOEV_SUCCESS if the operation was started, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   buf must remain valid until callback runs. At most one read may be pending
 *   on an event at a time.
 */
int nanoev_tcp_read(
    nanoev_event *event, 
    void *buf, 
    unsigned int len,
    nanoev_tcp_on_read callback
    );

/*
 * nanoev_tcp_shutdown
 *   Shut down reads, writes, or both directions on a connected TCP event.
 *
 * Parameters:
 *   event - TCP event.
 *   how   - NANOEV_TCP_SHUT_READ, NANOEV_TCP_SHUT_WRITE, or
 *           NANOEV_TCP_SHUT_BOTH.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   The TCP event must be connected. Pending read or write callbacks may still
 *   complete after shutdown according to platform socket semantics.
 */
int nanoev_tcp_shutdown(
    nanoev_event *event,
    int how
    );

/*
 * nanoev_tcp_addr
 *   Return the local or remote address for a TCP event.
 *
 * Parameters:
 *   event - TCP event.
 *   local - Non-zero for local address, zero for remote address.
 *   addr  - Output address.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   Connected TCP events support local and remote addresses. Listening TCP
 *   events support local addresses only.
 */
int nanoev_tcp_addr(
    nanoev_event *event, 
    int local,
    struct nanoev_addr *addr
    );

/*
 * nanoev_tcp_error
 *   Return the last socket error recorded on a TCP event.
 */
int nanoev_tcp_error(
    nanoev_event *event
    );

/*
 * nanoev_tcp_setopt
 *   Set a socket option on a TCP event.
 *
 * Parameters:
 *   event  - TCP event.
 *   level  - Socket option level.
 *   optname - Socket option name.
 *   optval - Socket option value.
 *   optlen - Socket option value length.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 */
int nanoev_tcp_setopt(
    nanoev_event *event,
    int level,
    int optname,
    const char *optval,
    int optlen
    );

/*
 * nanoev_tcp_getopt
 *   Get a socket option from a TCP event.
 *
 * Parameters:
 *   event  - TCP event.
 *   level  - Socket option level.
 *   optname - Socket option name.
 *   optval - Output buffer for the socket option value.
 *   optlen - Input/output buffer length.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 */
int nanoev_tcp_getopt(
    nanoev_event *event,
    int level,
    int optname,
    char *optval,
    int *optlen
    );

/*
 * nanoev_tcp_set_nodelay
 *   Enable or disable TCP_NODELAY on an open TCP socket.
 *
 * Parameters:
 *   event   - TCP event.
 *   enabled - Non-zero to enable, zero to disable.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 */
int nanoev_tcp_set_nodelay(
    nanoev_event *event,
    int enabled
    );

/*
 * nanoev_tcp_set_keepalive
 *   Enable or disable SO_KEEPALIVE on an open TCP socket.
 *
 * Parameters:
 *   event   - TCP event.
 *   enabled - Non-zero to enable, zero to disable.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 */
int nanoev_tcp_set_keepalive(
    nanoev_event *event,
    int enabled
    );

/*----------------------------------------------------------------------------*/

/*
 * nanoev_udp_on_read
 *   Callback invoked when a UDP read operation completes.
 *
 * Parameters:
 *   udp       - UDP event.
 *   status    - 0 on success, otherwise a platform socket error.
 *   buf       - Buffer passed to nanoev_udp_read().
 *   bytes     - Number of bytes read.
 *   from_addr - Sender address for the received datagram.
 */
typedef void (*nanoev_udp_on_read)(
    nanoev_event *udp,
    int status,
    void *buf,
    unsigned int bytes,
    const struct nanoev_addr *from_addr
    );

/*
 * nanoev_udp_on_write
 *   Callback invoked when a UDP write operation completes.
 *
 * Parameters:
 *   udp    - UDP event.
 *   status - 0 on success, otherwise a platform socket error.
 *   buf    - Buffer passed to nanoev_udp_write().
 *   bytes  - Number of bytes written.
 */
typedef void (*nanoev_udp_on_write)(
    nanoev_event *udp,
    int status,
    void *buf,
    unsigned int bytes
    );

/*
 * nanoev_udp_read
 *   Start one asynchronous UDP read operation.
 *
 * Parameters:
 *   event    - UDP event.
 *   buf      - Receive buffer.
 *   len      - Maximum number of bytes to read.
 *   callback - Completion callback.
 *
 * Returns:
 *   NANOEV_SUCCESS if the operation was started, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   buf must remain valid until callback runs. At most one read may be pending
 *   on an event at a time.
 */
int nanoev_udp_read(
    nanoev_event *event, 
    void *buf, 
    unsigned int len, 
    nanoev_udp_on_read callback
    );

/*
 * nanoev_udp_write
 *   Start one asynchronous UDP write operation.
 *
 * Parameters:
 *   event    - UDP event.
 *   buf      - Data buffer.
 *   len      - Number of bytes to write.
 *   to_addr  - Destination address.
 *   callback - Completion callback.
 *
 * Returns:
 *   NANOEV_SUCCESS if the operation was started, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   buf must remain valid until callback runs. At most one write may be pending
 *   on an event at a time.
 */
int nanoev_udp_write(
    nanoev_event *event, 
    const void *buf, 
    unsigned int len, 
    const struct nanoev_addr *to_addr,
    nanoev_udp_on_write callback
    );

/*
 * nanoev_udp_bind
 *   Bind a UDP event to an address.
 *
 * Parameters:
 *   event - UDP event.
 *   addr  - Local address.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 */
int nanoev_udp_bind(
    nanoev_event *event,
    const struct nanoev_addr *addr
    );

/*
 * nanoev_udp_addr
 *   Return the local address for a UDP event.
 *
 * Parameters:
 *   event - UDP event.
 *   addr  - Output address.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   The UDP event must have an open socket, for example after
 *   nanoev_udp_bind() or nanoev_udp_write().
 */
int nanoev_udp_addr(
    nanoev_event *event,
    struct nanoev_addr *addr
    );

/*
 * nanoev_udp_error
 *   Return the last socket error recorded on a UDP event.
 */
int nanoev_udp_error(
    nanoev_event *event
    );

/*
 * nanoev_udp_setopt
 *   Set a socket option on a UDP event.
 *
 * Parameters:
 *   event  - UDP event.
 *   level  - Socket option level.
 *   optname - Socket option name.
 *   optval - Socket option value.
 *   optlen - Socket option value length.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 */
int nanoev_udp_setopt(
    nanoev_event *event,
    int level,
    int optname,
    const char *optval,
    int optlen
    );

/*
 * nanoev_udp_getopt
 *   Get a socket option from a UDP event.
 *
 * Parameters:
 *   event  - UDP event.
 *   level  - Socket option level.
 *   optname - Socket option name.
 *   optval - Output buffer for the socket option value.
 *   optlen - Input/output buffer length.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 */
int nanoev_udp_getopt(
    nanoev_event *event,
    int level,
    int optname,
    char *optval,
    int *optlen
    );

/*----------------------------------------------------------------------------*/

/*
 * nanoev_async_callback
 *   Callback invoked when an async notification is handled by the loop.
 *
 * Parameters:
 *   async - Async event.
 */
typedef void (*nanoev_async_callback)(
    nanoev_event *async
    );

/*
 * nanoev_async_start
 *   Start an async event.
 *
 * Parameters:
 *   event    - Async event.
 *   callback - Callback invoked on the event loop thread.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 */
int nanoev_async_start(
    nanoev_event *event,
    nanoev_async_callback callback
    );

/*
 * nanoev_async_send
 *   Send an async notification.
 *
 * Parameters:
 *   event - Async event.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   May be called from another thread. Multiple sends may coalesce into one
 *   callback before the loop handles them.
 */
int nanoev_async_send(
    nanoev_event *event
    );

/*----------------------------------------------------------------------------*/

/*
 * nanoev_now
 *   Return the current system time.
 *
 * Parameters:
 *   now - Output time value.
 */
void nanoev_now(
    nanoev_timeval *now
    );

/*
 * nanoev_timer_callback
 *   Callback invoked when a timer fires.
 *
 * Parameters:
 *   timer - Timer event.
 */
typedef void (*nanoev_timer_callback)(
    nanoev_event *timer
    );

/*
 * nanoev_timer_add
 *   Add a timer.
 *
 * Parameters:
 *   event    - Timer event.
 *   after    - Delay before the timer fires.
 *   repeat   - Zero for one-shot, non-zero to repeat using the same interval.
 *   callback - Callback invoked when the timer fires.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   after.tv_sec and after.tv_usec must be non-negative, and after.tv_usec must
 *   be less than 1000000.
 */
int nanoev_timer_add(
    nanoev_event *event,
    nanoev_timeval after,
    int repeat,
    nanoev_timer_callback callback
    );

/*
 * nanoev_timer_del
 *   Delete a pending timer.
 *
 * Parameters:
 *   event - Timer event.
 *
 * Returns:
 *   NANOEV_SUCCESS on success, otherwise a NANOEV_ERROR_* code.
 *
 * Notes:
 *   May be called from the timer callback.
 */
int nanoev_timer_del(
    nanoev_event *event
    );

/*----------------------------------------------------------------------------*/

#endif  /* __NANOEV_H__ */

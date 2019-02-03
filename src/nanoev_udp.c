#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_udp {
    NANOEV_PROACTOR_FILEDS
    SOCKET sock;
    int error_code;
    io_context ctx_read;
    io_context ctx_write;
    io_buf buf_read;
    io_buf buf_write;
    socklen_t from_addr_len;
    struct sockaddr_in from_addr;
#ifndef _WIN32
    struct sockaddr_in to_addr;
#endif
    /* callback functions */
    nanoev_udp_on_write on_write;
    nanoev_udp_on_read  on_read;
};
typedef struct nanoev_udp nanoev_udp;

static void __udp_proactor_callback(nanoev_proactor *proactor, io_context *ctx);
static io_context* reactor_cb(nanoev_proactor *proactor, int events);

#define NANOEV_UDP_FLAG_WRITING      NANOEV_PROACTOR_FLAG_WRITING
#define NANOEV_UDP_FLAG_READING      NANOEV_PROACTOR_FLAG_READING
#define NANOEV_UDP_FLAG_ERROR        NANOEV_PROACTOR_FLAG_ERROR
#define NANOEV_UDP_FLAG_DELETED      NANOEV_PROACTOR_FLAG_DELETED

/*----------------------------------------------------------------------------*/

nanoev_event* udp_new(nanoev_loop *loop, void *userdata)
{
    nanoev_udp *udp;
    int error_code = 0;

    udp = (nanoev_udp*)mem_alloc(sizeof(nanoev_udp));
    if (!udp)
        return NULL;

    memset(udp, 0, sizeof(nanoev_udp));
    udp->type = nanoev_event_udp;
    udp->loop = loop;
    udp->userdata = userdata;
    udp->cb = __udp_proactor_callback;
#ifndef _WIN32
    udp->reactor_cb = reactor_cb;
#endif

    udp->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (INVALID_SOCKET == udp->sock) {
        error_code = socket_last_error();
        goto ERROR_EXIT;
    }

#ifdef _WIN32
    /* Make the socket non-inheritable */
    SetHandleInformation((HANDLE)udp->sock, HANDLE_FLAG_INHERIT, 0);
#endif

    if (!set_non_blocking(udp->sock, 1)) {
        error_code = socket_last_error();
        goto ERROR_EXIT;
    }

    error_code = register_proactor(udp->loop, (nanoev_proactor*)udp, udp->sock, _EV_READ);
    if (error_code)
        goto ERROR_EXIT;

    return (nanoev_event*)udp;

ERROR_EXIT:
    udp->error_code = error_code;
    udp->flags |= NANOEV_UDP_FLAG_ERROR;
    return (nanoev_event*)udp;
}

void udp_free(nanoev_event *event)
{
    nanoev_udp *udp = (nanoev_udp*)event;

    ASSERT(udp->type == nanoev_event_udp);

    if (udp->sock != INVALID_SOCKET) {
        close_socket(udp->sock);
        udp->sock = INVALID_SOCKET;
    }

    if ((udp->flags & NANOEV_UDP_FLAG_READING) || (udp->flags & NANOEV_UDP_FLAG_WRITING)) {
        /* lazy delete */
        add_endgame_proactor(udp->loop, (nanoev_proactor*)udp);
    } else {
        mem_free(udp);
    }
}

int nanoev_udp_read(
    nanoev_event *event, 
    void *buf, 
    unsigned int len, 
    nanoev_udp_on_read callback
    )
{
    nanoev_udp *udp = (nanoev_udp*)event;
#ifdef _WIN32
    DWORD flags = 0;
#endif

    ASSERT(udp);
    ASSERT(udp->type == nanoev_event_udp);
    ASSERT(in_loop_thread(udp->loop));

    if (!buf || !len || !callback)
        return NANOEV_ERROR_INVALID_ARG;
    if (udp->sock == INVALID_SOCKET
        || udp->flags & NANOEV_UDP_FLAG_ERROR
        || udp->flags & NANOEV_UDP_FLAG_DELETED
        || udp->flags & NANOEV_UDP_FLAG_READING
        )
        return NANOEV_ERROR_ACCESS_DENIED;

    udp->buf_read.buf = (char*)buf;
    udp->buf_read.len = len;
    memset(&udp->ctx_read, 0, sizeof(io_context));
    udp->from_addr_len = sizeof(udp->from_addr);

#ifdef _WIN32
    // MSDN:
    // ...
    // WSARecvFrom returns SOCKET_ERROR and indicates error code WSA_IO_PENDING. 
    // In this case, lpNumberOfBytesRecvd and lpFlags is not updated.
    // ...
    // Also, the values indicated by lpFrom and lpFromlen are not updated until completion is itself indicated. 
    // Applications must not use or disturb these values until they have been updated, 
    // therefore the application must not use automatic (that is, stack-based) variables for these parameters.
    if (0 != WSARecvFrom(udp->sock, &udp->buf_read, 1, NULL, &flags,
        (struct sockaddr*)&udp->from_addr, &udp->from_addr_len, &udp->ctx_read, NULL)
        && WSA_IO_PENDING != WSAGetLastError()
        ) {
        udp->flags |= NANOEV_UDP_FLAG_ERROR;
        udp->error_code = WSAGetLastError();
        return NANOEV_ERROR_FAIL;
    }
#endif

    udp->flags |= NANOEV_UDP_FLAG_READING;
    udp->on_read = callback;

    return NANOEV_SUCCESS;
}

int nanoev_udp_write(
    nanoev_event *event, 
    const void *buf, 
    unsigned int len, 
    const struct nanoev_addr *to_addr,
    nanoev_udp_on_write callback
    )
{
    nanoev_udp *udp = (nanoev_udp*)event;
    struct sockaddr_in addr;

    ASSERT(udp);
    ASSERT(udp->type == nanoev_event_udp);
    ASSERT(in_loop_thread(udp->loop));

    if (!buf || !len || !to_addr || !callback)
        return NANOEV_ERROR_INVALID_ARG;
    if (udp->sock == INVALID_SOCKET
        || udp->flags & NANOEV_UDP_FLAG_ERROR
        || udp->flags & NANOEV_UDP_FLAG_DELETED
        || udp->flags & NANOEV_UDP_FLAG_WRITING
        )
        return NANOEV_ERROR_ACCESS_DENIED;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = to_addr->ip;
    addr.sin_port = to_addr->port;

    udp->buf_write.buf = (char*)buf;
    udp->buf_write.len = len;
    memset(&udp->ctx_write, 0, sizeof(io_context));

#ifdef _WIN32
    if (0 != WSASendTo(udp->sock, &udp->buf_write, 1, NULL, 0,
        (struct sockaddr*)&addr, sizeof(addr), &udp->ctx_write, NULL)
        && WSA_IO_PENDING != WSAGetLastError()
        ) {
        udp->flags |= NANOEV_UDP_FLAG_ERROR;
        udp->error_code = WSAGetLastError();
        return NANOEV_ERROR_FAIL;
    }
#else
    memcpy(&(udp->to_addr), &addr, sizeof(addr));
    int ret = sendto(udp->sock, udp->buf_write.buf, udp->buf_write.len, 0, 
        (struct sockaddr*)&udp->to_addr, sizeof(udp->to_addr));
    if (ret > 0) {
        udp->ctx_write.status = 0;
        udp->ctx_write.bytes = ret;
        submit_fake_io(udp->loop, (nanoev_proactor*)udp, &udp->ctx_write);
    } else {
        if (errno != EAGAIN) {
            udp->flags |= NANOEV_UDP_FLAG_ERROR;
            udp->error_code = errno;
            return NANOEV_ERROR_FAIL;
        }
        ASSERT(!(udp->reactor_events & _EV_WRITE));
        if (0 != register_proactor(udp->loop, (nanoev_proactor*)udp, udp->sock, udp->reactor_events | _EV_WRITE)) {
            udp->flags |= NANOEV_UDP_FLAG_ERROR;
            udp->error_code = errno;
            return NANOEV_ERROR_FAIL;
        }
    }
#endif

    udp->flags |= NANOEV_UDP_FLAG_WRITING;
    udp->on_write = callback;

    return NANOEV_SUCCESS;
}

int nanoev_udp_bind(
    nanoev_event *event,
    const struct nanoev_addr *addr
    )
{
    nanoev_udp *udp = (nanoev_udp*)event;
    struct sockaddr_in local_addr;
    int ret_code;

    ASSERT(event);
    ASSERT(addr);

    if (udp->sock == INVALID_SOCKET
        || udp->flags & NANOEV_UDP_FLAG_ERROR
        || udp->flags & NANOEV_UDP_FLAG_DELETED
        )
        return NANOEV_ERROR_ACCESS_DENIED;

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = addr->ip;
    local_addr.sin_port = addr->port;
    ret_code = bind(udp->sock, (const struct sockaddr*)&local_addr, sizeof(local_addr));
    if (0 != ret_code) {
        udp->flags |= NANOEV_UDP_FLAG_ERROR;
        udp->error_code = socket_last_error();
        return NANOEV_ERROR_FAIL;
    }

    return NANOEV_SUCCESS;
}

int nanoev_udp_error(nanoev_event *event)
{
    nanoev_udp *udp = (nanoev_udp*)event;

    ASSERT(udp);
    ASSERT(udp->type == nanoev_event_udp);
    ASSERT(!(udp->flags & NANOEV_UDP_FLAG_DELETED));
    ASSERT(in_loop_thread(udp->loop));

    return udp->error_code;
}

int nanoev_udp_setopt(
    nanoev_event *event,
    int level,
    int optname,
    const char *optval,
    int optlen
    )
{
    nanoev_udp *udp = (nanoev_udp*)event;

    ASSERT(udp);
    ASSERT(udp->type == nanoev_event_udp);
    ASSERT(!(udp->flags & NANOEV_UDP_FLAG_DELETED));
    ASSERT(in_loop_thread(udp->loop));

    if (udp->sock == INVALID_SOCKET || udp->flags & NANOEV_UDP_FLAG_DELETED)
        return NANOEV_ERROR_ACCESS_DENIED;

    if (0 != setsockopt(udp->sock, level, optname, optval, optlen))
        return NANOEV_ERROR_FAIL;

    return NANOEV_SUCCESS;
}

int nanoev_udp_getopt(
    nanoev_event *event,
    int level,
    int optname,
    char *optval,
    int *optlen
    )
{
    nanoev_udp *udp = (nanoev_udp*)event;

    ASSERT(udp);
    ASSERT(udp->type == nanoev_event_udp);
    ASSERT(!(udp->flags & NANOEV_UDP_FLAG_DELETED));
    ASSERT(in_loop_thread(udp->loop));

    if (udp->sock == INVALID_SOCKET || udp->flags & NANOEV_UDP_FLAG_DELETED)
        return NANOEV_ERROR_ACCESS_DENIED;

    if (0 != getsockopt(udp->sock, level, optname, optval, (socklen_t*)optlen))
        return NANOEV_ERROR_FAIL;

    return NANOEV_SUCCESS;
}

/*----------------------------------------------------------------------------*/

void __udp_proactor_callback(nanoev_proactor *proactor, io_context *ctx)
{
    nanoev_udp *udp = (nanoev_udp*)proactor;
    nanoev_udp_on_write on_write;
    nanoev_udp_on_read on_read;
    int status;
    unsigned int bytes;
    struct nanoev_addr addr;

#ifdef _WIN32
    /**
       Internal : This member, which specifies a system-dependent status
       InternalHigh : This member, which specifies the length of the data transferred
     */
    status = ntstatus_to_winsock_error((long)ctx->Internal);
    bytes = (unsigned int)ctx->InternalHigh;
#else
    status = ctx->status;
    bytes = ctx->bytes;
#endif

    if (0 != status) {
        udp->flags |= NANOEV_UDP_FLAG_ERROR;
        udp->error_code = status;
    }

    if (&udp->ctx_read == ctx) {
        ASSERT(udp->flags & NANOEV_UDP_FLAG_READING);

        udp->flags &= ~NANOEV_UDP_FLAG_READING;
        on_read = udp->on_read;
        udp->on_read = NULL;
        if (!(udp->flags & NANOEV_UDP_FLAG_DELETED)) {
            ASSERT(on_read);
            addr.ip = udp->from_addr.sin_addr.s_addr;
            addr.port = udp->from_addr.sin_port;
            on_read((nanoev_event*)udp, status, udp->buf_read.buf, bytes, &addr);
        }

    } else {
        ASSERT(&udp->ctx_write == ctx);
        ASSERT(udp->flags & NANOEV_UDP_FLAG_WRITING);

#ifndef _WIN32
        if (udp->reactor_events & _EV_WRITE) {
            register_proactor(udp->loop, (nanoev_proactor*)udp, udp->sock, udp->reactor_events & ~_EV_WRITE);
        }
#endif

        udp->flags &= ~NANOEV_UDP_FLAG_WRITING;
        on_write = udp->on_write;
        udp->on_write = NULL;
        if (!(udp->flags & NANOEV_UDP_FLAG_DELETED)) {
            ASSERT(on_write);
            on_write((nanoev_event*)udp, status, udp->buf_write.buf, bytes);
        }
    }
}

#ifndef _WIN32
static io_context* reactor_cb(nanoev_proactor *proactor, int events)
{
    nanoev_udp *udp = (nanoev_udp*)proactor;

    if (events == _EV_READ) {
        int ret = recvfrom(udp->sock, udp->buf_read.buf, udp->buf_read.len, 0, 
            (struct sockaddr*)&udp->from_addr, &udp->from_addr_len);
        if (ret > 0) {
            udp->ctx_read.status = 0;
            udp->ctx_read.bytes = ret;
        } else {
            ASSERT(ret == -1);
            udp->ctx_read.status = errno;
            udp->ctx_read.bytes = 0;
        }
        return &(udp->ctx_read);

    } else {
        ASSERT(events == _EV_WRITE);
        int ret = sendto(udp->sock, udp->buf_write.buf, udp->buf_write.len, 0, 
            (struct sockaddr*)&udp->to_addr, sizeof(udp->to_addr));
        if (ret > 0) {
            udp->ctx_write.status = 0;
            udp->ctx_write.bytes = ret;
        } else {
            ASSERT(ret == -1);
            udp->ctx_write.status = errno;
            udp->ctx_write.bytes = 0;
        }
        return &(udp->ctx_write);
    }
}
#endif

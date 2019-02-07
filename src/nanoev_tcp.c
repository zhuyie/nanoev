#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

#define LOCAL_ADDR_BUF_LEN  (sizeof(struct sockaddr_storage) + 16)
#define REMOTE_ADDR_BUF_LEN (sizeof(struct sockaddr_storage) + 16)

struct nanoev_tcp {
    NANOEV_PROACTOR_FILEDS
    int family;
    SOCKET sock;
    int error_code;
    io_context ctx_read;
    io_context ctx_write;
    io_buf buf_read;
    io_buf buf_write;
    unsigned char *accept_addr_buf;
    /* callback functions */
    nanoev_tcp_on_write   on_write;
    nanoev_tcp_on_read    on_read;
    nanoev_tcp_on_connect on_connect;
    nanoev_tcp_on_accept  on_accept;
};
typedef struct nanoev_tcp nanoev_tcp;

static void tcp_proactor_callback(nanoev_proactor *proactor, io_context *ctx);
static nanoev_tcp* tcp_alloc_client(nanoev_loop *loop, void *userdata, int family, SOCKET socket);
static io_context* reactor_cb(nanoev_proactor *proactor, int events);
static int create_tcp_socket(nanoev_tcp *tcp, int family);
static int sockaddr_len(nanoev_tcp *tcp);

#define NANOEV_TCP_FLAG_CONNECTED    (0x00000001)      /* connection established */
#define NANOEV_TCP_FLAG_LISTENING    (0x00000002)      /* listening */
#define NANOEV_TCP_FLAG_WRITING      NANOEV_PROACTOR_FLAG_WRITING
#define NANOEV_TCP_FLAG_READING      NANOEV_PROACTOR_FLAG_READING
#define NANOEV_TCP_FLAG_ERROR        NANOEV_PROACTOR_FLAG_ERROR
#define NANOEV_TCP_FLAG_DELETED      NANOEV_PROACTOR_FLAG_DELETED

/*----------------------------------------------------------------------------*/

nanoev_event* tcp_new(nanoev_loop *loop, void *userdata)
{
    nanoev_tcp *tcp;

    tcp = (nanoev_tcp*)mem_alloc(sizeof(nanoev_tcp));
    if (!tcp)
        return NULL;

    memset(tcp, 0, sizeof(nanoev_tcp));
    tcp->type = nanoev_event_tcp;
    tcp->loop = loop;
    tcp->userdata = userdata;
    tcp->cb = tcp_proactor_callback;
#ifndef _WIN32
    tcp->reactor_cb = reactor_cb;
#endif
    tcp->sock = INVALID_SOCKET;

    return (nanoev_event*)tcp;
}

void tcp_free(nanoev_event *event)
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;

    ASSERT(tcp->type == nanoev_event_tcp);

    if (tcp->sock != INVALID_SOCKET) {
        close_socket(tcp->sock);
        tcp->sock = INVALID_SOCKET;
    }

    if ((tcp->flags & NANOEV_TCP_FLAG_READING) || (tcp->flags & NANOEV_TCP_FLAG_WRITING)) {
        /* lazy delete */
        add_endgame_proactor(tcp->loop, (nanoev_proactor*)tcp);
    } else {
        if (tcp->accept_addr_buf) {
            mem_free(tcp->accept_addr_buf);
            tcp->accept_addr_buf = NULL;
        }
        mem_free(tcp);
    }
}

int nanoev_tcp_connect(
    nanoev_event *event, 
    const struct nanoev_addr *server_addr,
    nanoev_tcp_on_connect callback
    )
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;
    int error_code = 0;
    struct sockaddr_storage local_addr;

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(in_loop_thread(tcp->loop));

    if (!server_addr || !callback)
        return NANOEV_ERROR_INVALID_ARG;
    if (tcp->sock != INVALID_SOCKET)
        return NANOEV_ERROR_ACCESS_DENIED;

    error_code = create_tcp_socket(tcp, server_addr->ss_family);
    if (0 != error_code)
        goto ERROR_EXIT;

#ifdef _WIN32
    error_code = register_proactor(tcp->loop, (nanoev_proactor*)tcp, tcp->sock, _EV_READ);
    if (0 != error_code)
        goto ERROR_EXIT;

    /* We have to call bind(...), or ConnectEx will failed with WSAEINVAL... */
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.ss_family = tcp->family;
    if (0 != bind(tcp->sock, (const struct sockaddr*)&local_addr, sockaddr_len(tcp))) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Call ConnectEx */
    if (!get_winsock_ext()->ConnectEx(tcp->sock, (const struct sockaddr*)server_addr, 
            sockaddr_len(tcp), NULL, 0, NULL, &tcp->ctx_write)
         && ERROR_IO_PENDING != WSAGetLastError()
        ) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }
#else
    int ret = connect(tcp->sock, (const struct sockaddr*)server_addr, sockaddr_len(tcp));
    if (ret == 0) {
        tcp->ctx_write.status = 0;
        tcp->ctx_write.bytes = 0;
        submit_fake_io(tcp->loop, (nanoev_proactor*)tcp, &tcp->ctx_write);
    } else {
        if (errno != EINPROGRESS) {
            error_code = errno;
            goto ERROR_EXIT;
        }
        ASSERT(!(tcp->reactor_events & _EV_WRITE));
        if (0 != register_proactor(tcp->loop, (nanoev_proactor*)tcp, tcp->sock, tcp->reactor_events | _EV_WRITE)) {
            error_code = errno;
            goto ERROR_EXIT;
        }
    }
#endif

    tcp->flags |= NANOEV_TCP_FLAG_WRITING;
    tcp->on_connect = callback;

    return NANOEV_SUCCESS;

ERROR_EXIT:
    tcp->error_code = error_code;
    tcp->flags |= NANOEV_TCP_FLAG_ERROR;
    return NANOEV_ERROR_FAIL;
}

int nanoev_tcp_listen(
    nanoev_event *event, 
    const struct nanoev_addr *local_addr,
    int backlog
    )
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;
    int error_code = 0;

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(in_loop_thread(tcp->loop));

    if (!local_addr)
        return NANOEV_ERROR_INVALID_ARG;
    if (tcp->sock != INVALID_SOCKET)
        return NANOEV_ERROR_ACCESS_DENIED;

    error_code = create_tcp_socket(tcp, local_addr->ss_family);
    if (0 != error_code)
        goto ERROR_EXIT;

    error_code = register_proactor(tcp->loop, (nanoev_proactor*)tcp, tcp->sock, _EV_READ);
    if (0 != error_code)
        goto ERROR_EXIT;

    /* bind */
    if (0 != bind(tcp->sock, (const struct sockaddr*)local_addr, sockaddr_len(tcp))) {
        error_code = socket_last_error();
        goto ERROR_EXIT;
    }

    /* listen */
    if (0 != listen(tcp->sock, (backlog == 0 ? SOMAXCONN : backlog))) {
        error_code = socket_last_error();
        goto ERROR_EXIT;
    }

#ifdef _WIN32
    /* Allocate the buffer which used in AcceptEx */
    tcp->accept_addr_buf = (unsigned char*)mem_alloc(LOCAL_ADDR_BUF_LEN + REMOTE_ADDR_BUF_LEN);
    if (!tcp->accept_addr_buf) {
        error_code = WSAENOBUFS;
        goto ERROR_EXIT;
    }
#endif

    /* We are listening now */
    tcp->flags |= NANOEV_TCP_FLAG_LISTENING;

    return NANOEV_SUCCESS;

ERROR_EXIT:
    tcp->error_code = error_code;
    tcp->flags |= NANOEV_TCP_FLAG_ERROR;
    return NANOEV_ERROR_FAIL;
}

int nanoev_tcp_accept(
    nanoev_event *event, 
    nanoev_tcp_on_accept callback, 
    nanoev_tcp_alloc_userdata alloc_userdata
    )
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;
    int error_code;
    SOCKET socket_accept = INVALID_SOCKET;
#ifdef _WIN32
    DWORD bytes;
#endif

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(in_loop_thread(tcp->loop));

    if (!callback)
        return NANOEV_ERROR_INVALID_ARG;
    if (tcp->sock == INVALID_SOCKET
        || tcp->flags & NANOEV_TCP_FLAG_ERROR
        || tcp->flags & NANOEV_TCP_FLAG_DELETED
        || !(tcp->flags & NANOEV_TCP_FLAG_LISTENING)
        )
        return NANOEV_ERROR_ACCESS_DENIED;

#ifdef _WIN32
    /* Open a socket for the accepted connection. */
    socket_accept = socket(tcp->family, SOCK_STREAM, 0);
    if (INVALID_SOCKET == socket_accept) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Make the socket non-inheritable */
    SetHandleInformation((HANDLE)socket_accept, HANDLE_FLAG_INHERIT, 0);

    /* call AcceptEx */
    memset(&tcp->ctx_read, 0, sizeof(io_context));
    ASSERT(tcp->accept_addr_buf);
    if (!get_winsock_ext()->AcceptEx(tcp->sock, socket_accept, tcp->accept_addr_buf, 
            0, LOCAL_ADDR_BUF_LEN, REMOTE_ADDR_BUF_LEN, 
            &bytes, &tcp->ctx_read)
         && ERROR_IO_PENDING != WSAGetLastError()
        ) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    tcp->buf_write.buf = (char*)socket_accept;  /* Tricky */
#else
    int fd = accept(tcp->sock, NULL, NULL);
    if (fd > 0) {
        tcp->ctx_read.status = 0;
        tcp->ctx_read.bytes = fd;
        tcp->buf_write.buf = (char*)(uintptr_t)fd;  /* Tricky */
        submit_fake_io(tcp->loop, (nanoev_proactor*)tcp, &tcp->ctx_read);
    } else {
        ASSERT(fd == -1);
        if (errno != EWOULDBLOCK) {
            error_code = errno;
            goto ERROR_EXIT;
        }
    }
#endif

    tcp->buf_read.buf = (char*)alloc_userdata;  /* Tricky */

    tcp->flags |= NANOEV_TCP_FLAG_READING;
    tcp->on_accept = callback;

    return NANOEV_SUCCESS;

ERROR_EXIT:
    if (socket_accept != INVALID_SOCKET)
        close_socket(socket_accept);
    tcp->error_code = error_code;
    tcp->flags |= NANOEV_TCP_FLAG_ERROR;
    return NANOEV_ERROR_FAIL;
}

int nanoev_tcp_write(
    nanoev_event *event, 
    const void *buf, 
    unsigned int len, 
    nanoev_tcp_on_write callback
    )
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;
#ifdef _WIN32
    DWORD cb;
#endif

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(in_loop_thread(tcp->loop));

    if (!buf || !len || !callback)
        return NANOEV_ERROR_INVALID_ARG;
    if (tcp->sock == INVALID_SOCKET
        || tcp->flags & NANOEV_TCP_FLAG_ERROR
        || tcp->flags & NANOEV_TCP_FLAG_DELETED
        || !(tcp->flags & NANOEV_TCP_FLAG_CONNECTED)
        || tcp->flags & NANOEV_TCP_FLAG_WRITING
        )
        return NANOEV_ERROR_ACCESS_DENIED;

    tcp->buf_write.buf = (char*)buf;
    tcp->buf_write.len = len;
    memset(&tcp->ctx_write, 0, sizeof(io_context));
    
#ifdef _WIN32
    if (0 != WSASend(tcp->sock, &tcp->buf_write, 1, &cb, 0, &tcp->ctx_write, NULL)
        && WSA_IO_PENDING != WSAGetLastError()
        ) {
        tcp->flags |= NANOEV_TCP_FLAG_ERROR;
        tcp->error_code = WSAGetLastError();
        return NANOEV_ERROR_FAIL;
    }
#else
    int ret = write(tcp->sock, buf, len);
    if (ret > 0) {
        tcp->ctx_write.status = 0;
        tcp->ctx_write.bytes = ret;
        submit_fake_io(tcp->loop, (nanoev_proactor*)tcp, &tcp->ctx_write);
    } else {
        if (errno != EAGAIN) {
            tcp->flags |= NANOEV_TCP_FLAG_ERROR;
            tcp->error_code = errno;
            return NANOEV_ERROR_FAIL;
        }
    }
#endif

    tcp->flags |= NANOEV_TCP_FLAG_WRITING;
    tcp->on_write = callback;

    return NANOEV_SUCCESS;
}

int nanoev_tcp_read(
    nanoev_event *event, 
    void *buf, 
    unsigned int len, 
    nanoev_tcp_on_read callback
    )
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;
#ifdef _WIN32
    DWORD cb, flags = 0;
#endif

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(in_loop_thread(tcp->loop));

    if (!buf || !len || !callback)
        return NANOEV_ERROR_INVALID_ARG;
    if (tcp->sock == INVALID_SOCKET
        || tcp->flags & NANOEV_TCP_FLAG_ERROR
        || tcp->flags & NANOEV_TCP_FLAG_DELETED
        || !(tcp->flags & NANOEV_TCP_FLAG_CONNECTED)
        || tcp->flags & NANOEV_TCP_FLAG_READING
        )
        return NANOEV_ERROR_ACCESS_DENIED;

    tcp->buf_read.buf = (char*)buf;
    tcp->buf_read.len = len;
    memset(&tcp->ctx_read, 0, sizeof(io_context));
    
#ifdef _WIN32
    if (0 != WSARecv(tcp->sock, &tcp->buf_read, 1, &cb, &flags, &tcp->ctx_read, NULL)
        && WSA_IO_PENDING != WSAGetLastError()
        ) {
        tcp->flags |= NANOEV_TCP_FLAG_ERROR;
        tcp->error_code = WSAGetLastError();
        return NANOEV_ERROR_FAIL;
    }
#endif

    tcp->flags |= NANOEV_TCP_FLAG_READING;
    tcp->on_read = callback;

    return NANOEV_SUCCESS;
}

int nanoev_tcp_addr(
    nanoev_event *event, 
    int local, 
    struct nanoev_addr *addr
    )
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;
    socklen_t len;
    int ret_code;

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(!(tcp->flags & NANOEV_TCP_FLAG_DELETED));
    ASSERT(in_loop_thread(tcp->loop));
    ASSERT(addr);

    if (tcp->sock == INVALID_SOCKET
        || tcp->flags & NANOEV_TCP_FLAG_ERROR
        || tcp->flags & NANOEV_TCP_FLAG_DELETED
        || !(tcp->flags & NANOEV_TCP_FLAG_CONNECTED)
        )
        return NANOEV_ERROR_ACCESS_DENIED;

    len = sockaddr_len(tcp);
    if (local) {
        ret_code = getsockname(tcp->sock, (struct sockaddr*)addr, &len);
    } else {
        ret_code = getpeername(tcp->sock, (struct sockaddr*)addr, &len);
    }
    if (0 != ret_code) {
        tcp->flags |= NANOEV_TCP_FLAG_ERROR;
        tcp->error_code = socket_last_error();
        return NANOEV_ERROR_FAIL;
    }

    return NANOEV_SUCCESS;
}

int nanoev_tcp_error(nanoev_event *event)
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(!(tcp->flags & NANOEV_TCP_FLAG_DELETED));
    ASSERT(in_loop_thread(tcp->loop));

    return tcp->error_code;
}

int nanoev_tcp_setopt(
    nanoev_event *event,
    int level,
    int optname,
    const char *optval,
    int optlen
    )
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(!(tcp->flags & NANOEV_TCP_FLAG_DELETED));
    ASSERT(in_loop_thread(tcp->loop));

    if (tcp->sock == INVALID_SOCKET || tcp->flags & NANOEV_TCP_FLAG_DELETED)
        return NANOEV_ERROR_ACCESS_DENIED;

    if (0 != setsockopt(tcp->sock, level, optname, optval, optlen))
        return NANOEV_ERROR_FAIL;

    return NANOEV_SUCCESS;
}

int nanoev_tcp_getopt(
    nanoev_event *event,
    int level,
    int optname,
    char *optval,
    int *optlen
    )
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(!(tcp->flags & NANOEV_TCP_FLAG_DELETED));
    ASSERT(in_loop_thread(tcp->loop));

    if (tcp->sock == INVALID_SOCKET || tcp->flags & NANOEV_TCP_FLAG_DELETED)
        return NANOEV_ERROR_ACCESS_DENIED;

    if (0 != getsockopt(tcp->sock, level, optname, optval, (socklen_t*)optlen))
        return NANOEV_ERROR_FAIL;

    return NANOEV_SUCCESS;
}

/*----------------------------------------------------------------------------*/

void tcp_proactor_callback(nanoev_proactor *proactor, io_context *ctx)
{
    nanoev_tcp *tcp = (nanoev_tcp*)proactor;
    nanoev_tcp_on_write on_write;
    nanoev_tcp_on_read on_read;
    nanoev_tcp_on_connect on_connect;
    nanoev_tcp_on_accept on_accept;
    nanoev_tcp_alloc_userdata alloc_userdata;
    SOCKET socket_accept;
    nanoev_tcp *tcp_new;
    void *userdata_new;
    int status;
    unsigned int bytes;
    int ret_code;

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

#ifndef _WIN32
    if (&tcp->ctx_write == ctx) {
        if (tcp->reactor_events & _EV_WRITE) {
            register_proactor(tcp->loop, (nanoev_proactor*)tcp, tcp->sock, tcp->reactor_events & ~_EV_WRITE);
        }
    }
#endif

    if (0 != status) {
        tcp->flags |= NANOEV_TCP_FLAG_ERROR;
        tcp->error_code = status;
    }

    if (tcp->flags & NANOEV_TCP_FLAG_CONNECTED) {
        if (&tcp->ctx_read == ctx) {
            /* a recv operation is completed */
            ASSERT(tcp->flags & NANOEV_TCP_FLAG_READING);
            tcp->flags &= ~NANOEV_TCP_FLAG_READING;
            on_read = tcp->on_read;
            tcp->on_read = NULL;
            if (!(tcp->flags & NANOEV_TCP_FLAG_DELETED)) {
                ASSERT(on_read);
                on_read((nanoev_event*)tcp, status, tcp->buf_read.buf, bytes);
            }

        } else {
            /* a send operation is completed */
            ASSERT(&tcp->ctx_write == ctx);
            ASSERT(tcp->flags & NANOEV_TCP_FLAG_WRITING);
            tcp->flags &= ~NANOEV_TCP_FLAG_WRITING;
            on_write = tcp->on_write;
            tcp->on_write = NULL;
            if (!(tcp->flags & NANOEV_TCP_FLAG_DELETED)) {
                ASSERT(on_write);
                on_write((nanoev_event*)tcp, status, tcp->buf_write.buf, bytes);
            }
        }

	} else {
        if (tcp->flags & NANOEV_TCP_FLAG_LISTENING) {
            /* an accept operation is completed */
            ASSERT(tcp->flags & NANOEV_TCP_FLAG_READING);
            tcp->flags &= ~NANOEV_TCP_FLAG_READING;

            on_accept = tcp->on_accept;
            tcp->on_accept = NULL;

            socket_accept = (SOCKET)tcp->buf_write.buf; /* Tricky */
            alloc_userdata = (nanoev_tcp_alloc_userdata)tcp->buf_read.buf;  /* Tricky */
            tcp_new = NULL;
            userdata_new = NULL;

            if (!(tcp->flags & NANOEV_TCP_FLAG_DELETED)) {
                if (0 != status) {
                    goto ON_ACCEPT_ERROR;
                }

            #ifdef _WIN32
                /**
                   When the AcceptEx function returns, the socket sAcceptSocket is in the default state
                   for a connected socket. The socket sAcceptSocket does not inherit the properties of the socket 
                   associated with sListenSocket parameter until SO_UPDATE_ACCEPT_CONTEXT is set on the socket. 
                 */
                ret_code = setsockopt(socket_accept, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, 
                    (char*)&tcp->sock, sizeof(tcp->sock));
                ASSERT(0 == ret_code);
            #endif

                /* alloc userdata */
                if (alloc_userdata
                    && !(userdata_new = alloc_userdata(tcp->userdata, NULL))
                    ) {
                    status = ENOMEM;
                    goto ON_ACCEPT_ERROR;
                }

                /* alloc a new tcp object */
                tcp_new = tcp_alloc_client(tcp->loop, userdata_new, tcp->family, socket_accept);
                if (!tcp_new) {
                    status = ENOMEM;
                    goto ON_ACCEPT_ERROR;
                }

            ON_ACCEPT_ERROR:
                if (0 != status) {
                    if (socket_accept != INVALID_SOCKET) {
                        close_socket(socket_accept);
                    }
                    if (userdata_new) {
                        alloc_userdata(tcp->userdata, userdata_new);  /* free userdata */
                    }
                }

                ASSERT(on_accept);
                on_accept((nanoev_event*)tcp, status, (nanoev_event*)tcp_new);

            } else {
                if (socket_accept != INVALID_SOCKET) {
                    close_socket(socket_accept);
                }
            }

        } else {
            /* a connect operation is completed */
            ASSERT(tcp->flags & NANOEV_TCP_FLAG_WRITING);
            tcp->flags &= ~NANOEV_TCP_FLAG_WRITING;

            on_connect = tcp->on_connect;
            tcp->on_connect = NULL;
            
            if (!(tcp->flags & NANOEV_TCP_FLAG_DELETED)) {
                if (0 == status) {
                    tcp->flags |= NANOEV_TCP_FLAG_CONNECTED;

                #ifdef _WIN32                    
                    /**
                       When the ConnectEx function returns, the socket s is in the default state
                       for a connected socket. The socket s does not enable previously set properties 
                       or options until SO_UPDATE_CONNECT_CONTEXT is set on the socket. 
                     */
                    ret_code = setsockopt(tcp->sock, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
                    ASSERT(0 == ret_code);
                #else
                    /* register _EV_READ */
                    register_proactor(tcp->loop, (nanoev_proactor*)tcp, tcp->sock, tcp->reactor_events | _EV_READ);
                #endif
                }

                ASSERT(on_connect);
                on_connect((nanoev_event*)tcp, status);
            }
        }
    }
}

nanoev_tcp* tcp_alloc_client(nanoev_loop *loop, void *userdata, int family, SOCKET socket)
{
    nanoev_tcp *tcp = (nanoev_tcp*)tcp_new(loop, userdata);
    if (!tcp)
        return NULL;

    if (!set_non_blocking(socket, 1)) {
        tcp_free((nanoev_event*)tcp);
        return NULL;
    }

    if (register_proactor(loop, (nanoev_proactor*)tcp, socket, _EV_READ)) {
        tcp_free((nanoev_event*)tcp);
        return NULL;
    }

    tcp->flags |= NANOEV_TCP_FLAG_CONNECTED;
    tcp->family = family;
    tcp->sock = socket;
    
    return tcp;
}


#ifndef _WIN32
static io_context* reactor_cb(nanoev_proactor *proactor, int events)
{
    nanoev_tcp *tcp = (nanoev_tcp*)proactor;

    if (events == _EV_READ) {
        if (!(tcp->flags & NANOEV_TCP_FLAG_READING)) {
            return NULL;
        }

        if (tcp->flags & NANOEV_TCP_FLAG_CONNECTED) {
            /* read */
            int ret = read(tcp->sock, tcp->buf_read.buf, tcp->buf_read.len);
            if (ret >= 0) {
                tcp->ctx_read.status = 0;
                tcp->ctx_read.bytes = ret;
            } else {
                ASSERT(ret == -1);
                tcp->ctx_read.status = errno;
                tcp->ctx_read.bytes = 0;
            }
            return &(tcp->ctx_read);

        } else {
            /* accept */
            int fd = accept(tcp->sock, NULL, NULL);
            if (fd > 0) {
                tcp->ctx_read.status = 0;
                tcp->ctx_read.bytes = 0;
                tcp->buf_write.buf = (char*)(uintptr_t)fd;  /* Tricky */
            } else {
                ASSERT(fd == -1);
                tcp->ctx_read.status = errno;
                tcp->ctx_read.bytes = 0;
                tcp->buf_write.buf = (char*)(uintptr_t)0;  /* Tricky */
            }
            return &(tcp->ctx_read);
        }

    } else {
        ASSERT(events == _EV_WRITE);
        if (!(tcp->flags & NANOEV_TCP_FLAG_WRITING)) {
            return NULL;
        }

        if (tcp->flags & NANOEV_TCP_FLAG_CONNECTED) {
            /* write */
            int ret = write(tcp->sock, tcp->buf_write.buf, tcp->buf_write.len);
            if (ret > 0) {
                tcp->ctx_write.status = 0;
                tcp->ctx_write.bytes = ret;
            } else {
                ASSERT(ret == -1);
                tcp->ctx_write.status = errno;
                tcp->ctx_write.bytes = 0;
            }
            return &(tcp->ctx_write);

        } else {
            /* connect */
            int result;
            socklen_t result_len = sizeof(result);
            if (getsockopt(tcp->sock, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0) {
                tcp->ctx_write.status = errno;
                tcp->ctx_write.bytes = 0;
                return &(tcp->ctx_write);
            }
            tcp->ctx_write.status = result;
            tcp->ctx_write.bytes = 0;
            return &(tcp->ctx_write);
        }
    }
}
#endif

static int create_tcp_socket(nanoev_tcp *tcp, int family)
{
    int error_code = 0;
    ASSERT(tcp->sock == INVALID_SOCKET);
    ASSERT(family == AF_INET || family == AF_INET6);

    tcp->family = family;
    
    tcp->sock = socket(family, SOCK_STREAM, 0);
    if (INVALID_SOCKET == tcp->sock) {
        error_code = socket_last_error();
        goto ERROR_EXIT;
    }

#ifdef _WIN32
    /* Make the socket non-inheritable */
    SetHandleInformation((HANDLE)tcp->sock, HANDLE_FLAG_INHERIT, 0);
#endif

    if (!set_non_blocking(tcp->sock, 1)) {
        error_code = socket_last_error();
        goto ERROR_EXIT;
    }

ERROR_EXIT:
    return error_code;
}

static int sockaddr_len(nanoev_tcp *tcp)
{
    if (tcp->family == AF_INET) {
        return sizeof(struct sockaddr_in);
    } else {
        ASSERT(tcp->family == AF_INET6);
        return sizeof(struct sockaddr_in6);
    }
}

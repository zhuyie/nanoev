#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

#define LOCAL_ADDR_BUF_LEN  (sizeof(struct sockaddr_in) + 16)
#define REMOTE_ADDR_BUF_LEN (sizeof(struct sockaddr_in) + 16)

struct nanoev_tcp {
    NANOEV_PROACTOR_FILEDS
    SOCKET sock;
    int error_code;
    OVERLAPPED overlapped_read;
    OVERLAPPED overlapped_write;
    WSABUF buf_read;
    WSABUF buf_write;
    unsigned char *accept_addr_buf;
    /* callback functions */
    nanoev_tcp_on_write   on_write;
    nanoev_tcp_on_read    on_read;
    nanoev_tcp_on_connect on_connect;
    nanoev_tcp_on_accept  on_accept;
};
typedef struct nanoev_tcp nanoev_tcp;

static void __tcp_proactor_callback(nanoev_proactor *proactor, LPOVERLAPPED overlapped);
static nanoev_tcp* __tcp_alloc_client(nanoev_loop *loop, void *userdata, SOCKET socket);

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
    tcp->callback = __tcp_proactor_callback;
    tcp->sock = INVALID_SOCKET;

    return (nanoev_event*)tcp;
}

void tcp_free(nanoev_event *event)
{
    nanoev_tcp *tcp = (nanoev_tcp*)event;

    ASSERT(tcp->type == nanoev_event_tcp);

    if (tcp->sock != INVALID_SOCKET) {
        closesocket(tcp->sock);
        tcp->sock = INVALID_SOCKET;
    }

    if (tcp->accept_addr_buf) {
        mem_free(tcp->accept_addr_buf);
        tcp->accept_addr_buf = NULL;
    }

    if ((tcp->flags & NANOEV_TCP_FLAG_READING) || (tcp->flags & NANOEV_TCP_FLAG_WRITING)) {
        /* lazy delete */
        add_endgame_proactor(tcp->loop, (nanoev_proactor*)tcp);
    } else {
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
    unsigned long is_nonblocking = 1;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(in_loop_thread(tcp->loop));

    if (!server_addr || !callback)
        return NANOEV_ERROR_INVALID_ARG;
    if (tcp->sock != INVALID_SOCKET)
        return NANOEV_ERROR_ACCESS_DENIED;

    /* Create a new socket */
    tcp->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (INVALID_SOCKET == tcp->sock) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Make the socket non-inheritable */
    SetHandleInformation((HANDLE)tcp->sock, HANDLE_FLAG_INHERIT, 0);

    /* Set the socket into nonblocking mode */
    if (0 != ioctlsocket(tcp->sock, FIONBIO, &is_nonblocking)) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Associate the socket with IOCP */
    error_code = register_proactor_to_loop((nanoev_proactor*)tcp, tcp->sock, tcp->loop);
    if (error_code)
        goto ERROR_EXIT;

    /* We have to call bind(...), or ConnectEx will failed with WSAEINVAL... */
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(0);
    if (0 != bind(tcp->sock, (const struct sockaddr*)&local_addr, sizeof(local_addr))) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Call ConnectEx */
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = server_addr->ip;
    remote_addr.sin_port = server_addr->port;
    if (!get_winsock_ext()->ConnectEx(tcp->sock, (const struct sockaddr*)&remote_addr, 
            sizeof(remote_addr), NULL, 0, NULL, &tcp->overlapped_write)
         && ERROR_IO_PENDING != WSAGetLastError()
        ) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    add_outstanding_io(tcp->loop);
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
    unsigned long is_nonblocking = 1;
    struct sockaddr_in addr;

    ASSERT(tcp);
    ASSERT(tcp->type == nanoev_event_tcp);
    ASSERT(in_loop_thread(tcp->loop));

    if (!local_addr)
        return NANOEV_ERROR_INVALID_ARG;
    if (tcp->sock != INVALID_SOCKET)
        return NANOEV_ERROR_ACCESS_DENIED;

    /* Create a new socket */
    tcp->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (INVALID_SOCKET == tcp->sock) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Make the socket non-inheritable */
    SetHandleInformation((HANDLE)tcp->sock, HANDLE_FLAG_INHERIT, 0);

    /* Set the socket into nonblocking mode */
    if (0 != ioctlsocket(tcp->sock, FIONBIO, &is_nonblocking)) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Associate the socket with IOCP */
    error_code = register_proactor_to_loop((nanoev_proactor*)tcp, tcp->sock, tcp->loop);
    if (error_code)
        goto ERROR_EXIT;

    /* bind */
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = local_addr->ip;
    addr.sin_port = local_addr->port;
    if (0 != bind(tcp->sock, (const struct sockaddr*)&addr, sizeof(addr))) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* listen */
    if (0 != listen(tcp->sock, (backlog == 0 ? SOMAXCONN : backlog))) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Allocate the buffer which used in AcceptEx */
    tcp->accept_addr_buf = (unsigned char*)mem_alloc(LOCAL_ADDR_BUF_LEN + REMOTE_ADDR_BUF_LEN);
    if (!tcp->accept_addr_buf) {
        error_code = WSAENOBUFS;
        goto ERROR_EXIT;
    }

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
    DWORD bytes;

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

    /* Open a socket for the accepted connection. */
    socket_accept = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (INVALID_SOCKET == socket_accept) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Make the socket non-inheritable */
    SetHandleInformation((HANDLE)socket_accept, HANDLE_FLAG_INHERIT, 0);

    /* call AcceptEx */
    memset(&tcp->overlapped_read, 0, sizeof(OVERLAPPED));
    ASSERT(tcp->accept_addr_buf);
    if (!get_winsock_ext()->AcceptEx(tcp->sock, socket_accept, tcp->accept_addr_buf, 
            0, LOCAL_ADDR_BUF_LEN, REMOTE_ADDR_BUF_LEN, 
            &bytes, &tcp->overlapped_read)
         && ERROR_IO_PENDING != WSAGetLastError()
        ) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    add_outstanding_io(tcp->loop);
    tcp->buf_write.buf = (char*)socket_accept;
    tcp->overlapped_write.Internal = (ULONG_PTR)alloc_userdata;  /* Tricky */
    tcp->flags |= NANOEV_TCP_FLAG_READING;
    tcp->on_accept = callback;

    return NANOEV_SUCCESS;

ERROR_EXIT:
    if (socket_accept != INVALID_SOCKET)
        closesocket(socket_accept);
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
    DWORD cb;

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
    memset(&tcp->overlapped_write, 0, sizeof(OVERLAPPED));
    if (0 != WSASend(tcp->sock, &tcp->buf_write, 1, &cb, 0, &tcp->overlapped_write, NULL)
        && WSA_IO_PENDING != WSAGetLastError()
        ) {
        tcp->flags |= NANOEV_TCP_FLAG_ERROR;
        tcp->error_code = WSAGetLastError();
        return NANOEV_ERROR_FAIL;
    }

    add_outstanding_io(tcp->loop);
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
    DWORD cb, flags = 0;

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
    memset(&tcp->overlapped_read, 0, sizeof(OVERLAPPED));
    if (0 != WSARecv(tcp->sock, &tcp->buf_read, 1, &cb, &flags, &tcp->overlapped_read, NULL)
        && WSA_IO_PENDING != WSAGetLastError()
        ) {
        tcp->flags |= NANOEV_TCP_FLAG_ERROR;
        tcp->error_code = WSAGetLastError();
        return NANOEV_ERROR_FAIL;
    }

    add_outstanding_io(tcp->loop);
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
    struct sockaddr_in sock_addr;
    int len;
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

    len = sizeof(struct sockaddr_in);
    if (local) {
        ret_code = getsockname(tcp->sock, (struct sockaddr*)&sock_addr, &len);
    } else {
        ret_code = getpeername(tcp->sock, (struct sockaddr*)&sock_addr, &len);
    }
    if (0 != ret_code) {
        tcp->flags |= NANOEV_TCP_FLAG_ERROR;
        tcp->error_code = WSAGetLastError();
        return NANOEV_ERROR_FAIL;
    }

    addr->ip = sock_addr.sin_addr.s_addr;
    addr->port = sock_addr.sin_port;
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

/*----------------------------------------------------------------------------*/

void __tcp_proactor_callback(nanoev_proactor *proactor, LPOVERLAPPED overlapped)
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

    /**
       Internal : This member, which specifies a system-dependent status
       InternalHigh : This member, which specifies the length of the data transferred
     */
    status = ntstatus_to_winsock_error(overlapped->Internal);
    bytes = overlapped->InternalHigh;

    if (0 != status) {
        tcp->flags |= NANOEV_TCP_FLAG_ERROR;
        tcp->error_code = status;
    }

    if (tcp->flags & NANOEV_TCP_FLAG_CONNECTED) {

        if (&tcp->overlapped_read == overlapped) {
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
            ASSERT(&tcp->overlapped_write == overlapped);
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

            socket_accept = (SOCKET)tcp->buf_write.buf;
            alloc_userdata = (nanoev_tcp_alloc_userdata)tcp->overlapped_write.Internal;  /* Tricky */
            tcp_new = NULL;
            userdata_new = NULL;

            if (!(tcp->flags & NANOEV_TCP_FLAG_DELETED)) {

                if (status) {
                    goto ON_ACCEPT_ERROR;
                }

                /**
                   When the AcceptEx function returns, the socket sAcceptSocket is in the default state
                   for a connected socket. The socket sAcceptSocket does not inherit the properties of the socket 
                   associated with sListenSocket parameter until SO_UPDATE_ACCEPT_CONTEXT is set on the socket. 
                 */
                ret_code = setsockopt(socket_accept, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, 
                    (char*)&tcp->sock, sizeof(tcp->sock));
                ASSERT(0 == ret_code);

                /* alloc userdata */
                if (alloc_userdata
                    && !(userdata_new = alloc_userdata(tcp->userdata, NULL))
                    ) {
                    status = WSAENOBUFS;
                    goto ON_ACCEPT_ERROR;
                }

                /* alloc a new tcp object */
                tcp_new = __tcp_alloc_client(tcp->loop, userdata_new, socket_accept);
                if (!tcp_new) {
                    status = WSAENOBUFS;
                    goto ON_ACCEPT_ERROR;
                }

ON_ACCEPT_ERROR:
                if (status) {
                    closesocket(socket_accept);
                    if (userdata_new) {
                        alloc_userdata(tcp->userdata, userdata_new);  /* free userdata */
                    }
                }

                ASSERT(on_accept);
                on_accept((nanoev_event*)tcp, status, (nanoev_event*)tcp_new);

            } else {
                closesocket(socket_accept);
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
                    /**
                       When the ConnectEx function returns, the socket s is in the default state
                       for a connected socket. The socket s does not enable previously set properties 
                       or options until SO_UPDATE_CONNECT_CONTEXT is set on the socket. 
                     */
                    ret_code = setsockopt(tcp->sock, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
                    ASSERT(0 == ret_code);
                }

                ASSERT(on_connect);
                on_connect((nanoev_event*)tcp, status);
            }
        }

    }
}

nanoev_tcp* __tcp_alloc_client(nanoev_loop *loop, void *userdata, SOCKET socket)
{
    nanoev_tcp *tcp = (nanoev_tcp*)tcp_new(loop, userdata);
    if (!tcp)
        return NULL;

    /* Associate the socket with IOCP */
    if (register_proactor_to_loop((nanoev_proactor*)tcp, socket, loop)) {
        tcp_free((nanoev_event*)tcp);
        return NULL;
    }

    tcp->flags |= NANOEV_TCP_FLAG_CONNECTED;
    tcp->sock = socket;
    
    return tcp;
}

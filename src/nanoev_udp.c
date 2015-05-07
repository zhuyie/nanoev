#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_udp {
    NANOEV_PROACTOR_FILEDS
    SOCKET sock;
    int error_code;
    OVERLAPPED overlapped_read;
    OVERLAPPED overlapped_write;
    WSABUF buf_read;
    WSABUF buf_write;
    struct sockaddr_in from_addr;
    /* callback functions */
    nanoev_udp_on_write on_write;
    nanoev_udp_on_read  on_read;
};
typedef struct nanoev_udp nanoev_udp;

static void __udp_proactor_callback(nanoev_proactor *proactor, LPOVERLAPPED overlapped);

#define NANOEV_UDP_FLAG_WRITING      NANOEV_PROACTOR_FLAG_WRITING
#define NANOEV_UDP_FLAG_READING      NANOEV_PROACTOR_FLAG_READING
#define NANOEV_UDP_FLAG_ERROR        NANOEV_PROACTOR_FLAG_ERROR
#define NANOEV_UDP_FLAG_DELETED      NANOEV_PROACTOR_FLAG_DELETED

/*----------------------------------------------------------------------------*/

nanoev_event* udp_new(nanoev_loop *loop, void *userdata)
{
    nanoev_udp *udp;
    int error_code = 0;
    unsigned long is_nonblocking = 1;

    udp = (nanoev_udp*)mem_alloc(sizeof(nanoev_udp));
    if (!udp)
        return NULL;

    memset(udp, 0, sizeof(nanoev_udp));
    udp->type = nanoev_event_udp;
    udp->loop = loop;
    udp->userdata = userdata;
    udp->callback = __udp_proactor_callback;

    /* Create a new socket */
    udp->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (INVALID_SOCKET == udp->sock) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Make the socket non-inheritable */
    SetHandleInformation((HANDLE)udp->sock, HANDLE_FLAG_INHERIT, 0);

    /* Set the socket into nonblocking mode */
    if (0 != ioctlsocket(udp->sock, FIONBIO, &is_nonblocking)) {
        error_code = WSAGetLastError();
        goto ERROR_EXIT;
    }

    /* Associate the socket with IOCP */
    error_code = register_proactor_to_loop((nanoev_proactor*)udp, udp->sock, udp->loop);
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
        closesocket(udp->sock);
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
    const void *buf, 
    unsigned int len, 
    nanoev_udp_on_read callback
    )
{
    nanoev_udp *udp = (nanoev_udp*)event;
    DWORD flags = 0;
    int from_len;

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
    memset(&udp->overlapped_read, 0, sizeof(udp->overlapped_read));
    from_len = sizeof(udp->from_addr);
    if (0 != WSARecvFrom(udp->sock, &udp->buf_write, 1, NULL, &flags,
        (struct sockaddr*)&udp->from_addr, &from_len, (OVERLAPPED*)&udp->overlapped_read, NULL)
        && WSA_IO_PENDING != WSAGetLastError()
        ) {
        udp->flags |= NANOEV_UDP_FLAG_ERROR;
        udp->error_code = WSAGetLastError();
        return NANOEV_ERROR_FAIL;
    }

    add_outstanding_io(udp->loop);
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

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = to_addr->ip;
    addr.sin_port = to_addr->port;

    udp->buf_write.buf = (char*)buf;
    udp->buf_write.len = len;
    memset(&udp->overlapped_write, 0, sizeof(udp->overlapped_write));
    if (0 != WSASendTo(udp->sock, &udp->buf_write, 1, NULL, 0,
        (struct sockaddr*)&addr, sizeof(addr), (OVERLAPPED*)&udp->overlapped_write, NULL)
        && WSA_IO_PENDING != WSAGetLastError()
        ) {
        udp->flags |= NANOEV_UDP_FLAG_ERROR;
        udp->error_code = WSAGetLastError();
        return NANOEV_ERROR_FAIL;
    }

    add_outstanding_io(udp->loop);
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
        udp->error_code = WSAGetLastError();
        return NANOEV_ERROR_FAIL;
    }

    return NANOEV_SUCCESS;
}

/*----------------------------------------------------------------------------*/

void __udp_proactor_callback(nanoev_proactor *proactor, LPOVERLAPPED overlapped)
{
    nanoev_udp *udp = (nanoev_udp*)proactor;
    nanoev_udp_on_write on_write;
    nanoev_udp_on_read on_read;
    int status;
    unsigned int bytes;
    struct nanoev_addr addr;

    /**
       Internal : This member, which specifies a system-dependent status
       InternalHigh : This member, which specifies the length of the data transferred
     */
    status = ntstatus_to_winsock_error(overlapped->Internal);
    bytes = overlapped->InternalHigh;

    if (0 != status) {
        udp->flags |= NANOEV_UDP_FLAG_ERROR;
        udp->error_code = status;
    }

    if (&udp->overlapped_read == overlapped) {
        ASSERT(udp->flags &= NANOEV_UDP_FLAG_READING);

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
        ASSERT(&udp->overlapped_write == overlapped);
        ASSERT(udp->flags &= NANOEV_UDP_FLAG_WRITING);

        udp->flags &= ~NANOEV_UDP_FLAG_WRITING;
        on_write = udp->on_write;
        udp->on_write = NULL;
        if (!(udp->flags & NANOEV_UDP_FLAG_DELETED)) {
            ASSERT(on_write);
            on_write((nanoev_event*)udp, status, udp->buf_write.buf, bytes);
        }
    }
}

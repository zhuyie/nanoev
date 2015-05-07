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
    /* callback functions */
};
typedef struct nanoev_udp nanoev_udp;

static void __udp_proactor_callback(nanoev_proactor *proactor, LPOVERLAPPED overlapped);

#define NANOEV_UDP_FLAG_WRITING      NANOEV_PROACTOR_FLAG_WRITING
#define NANOEV_UDP_FLAG_READING      NANOEV_PROACTOR_FLAG_READING

/*----------------------------------------------------------------------------*/

nanoev_event* udp_new(nanoev_loop *loop, void *userdata)
{
    nanoev_udp *udp;

    udp = (nanoev_udp*)mem_alloc(sizeof(nanoev_udp));
    if (!udp)
        return NULL;

    memset(udp, 0, sizeof(nanoev_udp));
    udp->type = nanoev_event_udp;
    udp->loop = loop;
    udp->userdata = userdata;
    udp->callback = __udp_proactor_callback;
    udp->sock = INVALID_SOCKET;

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

/*----------------------------------------------------------------------------*/

void __udp_proactor_callback(nanoev_proactor *proactor, LPOVERLAPPED overlapped)
{
}

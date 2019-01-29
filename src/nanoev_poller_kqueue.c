#ifdef __APPLE__

#include "nanoev_poller.h"
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <errno.h>
#include <stdio.h>

/*----------------------------------------------------------------------------*/

typedef struct _kqueue_poller {
    int kq;
} _kqueue_poller;

poller kqueue_poller_create()
{
    _kqueue_poller *p = (_kqueue_poller*)mem_alloc(sizeof(_kqueue_poller));
    if (!p)
        return NULL;

    p->kq = kqueue();
    if (p->kq == -1) {
        mem_free(p);
        return NULL;
    }

    return p;
}

void kqueue_poller_destroy(poller p)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    close(_p->kq);
    mem_free(_p);
}

int kqueue_poller_modify(poller p, SOCKET fd, nanoev_proactor *proactor, int events)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    return ret;
}

int kqueue_poller_poll(poller p, poller_event *events, int max_events, const struct nanoev_timeval *timeout)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    return -1;
}

/*----------------------------------------------------------------------------*/

poller_impl _nanoev_poller_impl = {
    .poller_create  = kqueue_poller_create,
    .poller_destroy = kqueue_poller_destroy,
    .poller_modify  = kqueue_poller_modify,
    .poller_poll    = kqueue_poller_poll,
};

/*----------------------------------------------------------------------------*/

#endif /* __APPLE__ */

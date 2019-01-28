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

int kqueue_poller_add(poller p, SOCKET fd, nanoev_proactor *proactor, int events)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    struct kevent changes[1];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_ADD, 0, 0, proactor);

    int ret = kevent(_p->kq, changes, 1, NULL, 0, NULL);
    if (ret != 0) {
        printf("kqueue_poller_add kevent ret=%d, errno=%d\n", ret, errno);
    }
    return ret;
}

int kqueue_poller_mod(poller p, SOCKET fd, nanoev_proactor *proactor, int events)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    struct kevent changes[1];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_ADD, 0, 0, proactor);

    int ret = kevent(_p->kq, changes, 1, NULL, 0, NULL);
    if (ret != 0) {
        printf("kqueue_poller_mod kevent ret=%d, errno=%d\n", ret, errno);
    }
    return ret;
}

int kqueue_poller_del(poller p, SOCKET fd)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    struct kevent changes[1];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);

    int ret = kevent(_p->kq, changes, 1, NULL, 0, NULL);
    if (ret != 0) {
        printf("kqueue_poller_del kevent ret=%d, errno=%d\n", ret, errno);
    }
    return ret;
}

int kqueue_poller_wait(poller p, poller_event *events, int max_events, const struct nanoev_timeval *timeout)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    return -1;
}

/*----------------------------------------------------------------------------*/

poller_impl _nanoev_poller_impl = {
    .poller_create  = kqueue_poller_create,
    .poller_destroy = kqueue_poller_destroy,
    .poller_add     = kqueue_poller_add,
    .poller_mod     = kqueue_poller_mod,
    .poller_del     = kqueue_poller_del,
    .poller_wait    = kqueue_poller_wait,
};

/*----------------------------------------------------------------------------*/

#endif /* __APPLE__ */

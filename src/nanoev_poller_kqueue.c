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
    poller_event *events;
    int events_count;
    int events_capacity;
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

    struct kevent kev[1];
    EV_SET(kev, 1, EVFILT_USER, EV_ADD, 0, 0, NULL);
    int ret = kevent(p->kq, kev, 1, NULL, 0, NULL);
    if (ret != 0) {
        printf("kqueue_poller_create kevent ret=%d,errno=%d\n", ret, errno);
        close(p->kq);
        mem_free(p);
        return NULL;
    }

    p->events = NULL;
    p->events_count = 0;
    p->events_capacity = 0;

    return p;
}

void kqueue_poller_destroy(poller p)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    close(_p->kq);
    mem_free(_p->events);
    mem_free(_p);
}

int kqueue_poller_modify(poller p, SOCKET fd, nanoev_proactor *proactor, int events)
{
    struct kevent changes[1];
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    int reactor_events = proactor->reactor_events;
    if (reactor_events == events) {
	    //printf("kqueue_poller_modify fd=%d,events=%d no change\n", fd, reactor_events);
        return 0;
    }

    changes[0].flags = 0;
    if ((events & _EV_READ) && !(reactor_events & _EV_READ)) {
        EV_SET(&changes[0], fd, EVFILT_READ, EV_ADD, 0, 0, proactor);
        reactor_events |= _EV_READ;
    } else if (!(events & _EV_READ) && (reactor_events & _EV_READ)) {
        EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, proactor);
        reactor_events &= ~_EV_READ;
    }
    if (changes[0].flags) {
        int ret = kevent(_p->kq, changes, 1, NULL, 0, NULL);
        if (ret != 0) {
            printf("kqueue_poller_modify kevent(r) ret=%d,errno=%d\n", ret, errno);
            return -1;
        }
        proactor->reactor_events = reactor_events;
    }


    changes[0].flags = 0;
    if ((events & _EV_WRITE) && !(reactor_events & _EV_WRITE)) {
        EV_SET(&changes[0], fd, EVFILT_WRITE, EV_ADD, 0, 0, proactor);
        reactor_events |= _EV_WRITE;
    } else if (!(events & _EV_WRITE) && (reactor_events & _EV_WRITE)) {
        EV_SET(&changes[0], fd, EVFILT_WRITE, EV_DELETE, 0, 0, proactor);
        reactor_events &= ~_EV_WRITE;
    }
    if (changes[0].flags) {
        int ret = kevent(_p->kq, changes, 1, NULL, 0, NULL);
        if (ret != 0) {
            printf("kqueue_poller_modify kevent(w) ret=%d,errno=%d\n", ret, errno);
            return -1;
        }
        proactor->reactor_events = reactor_events;
    }

    //printf("kqueue_poller_modify fd=%d,events=%d\n", fd, reactor_events);
    return 0;
}

int kqueue_poller_poll(poller p, poller_event *events, int max_events, const nanoev_timeval *timeout)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);
    int count0 = 0, count1 = 0;

    if (_p->events_count > 0) {
        count0 = _p->events_count;
        if (count0 > max_events)
            count0 = max_events;

        memcpy(events, _p->events, sizeof(poller_event)*count0);
        _p->events_count -= count0;
        if (_p->events_count > 0) {
            memcpy(_p->events, _p->events+count0, sizeof(poller_event)*_p->events_count);
        }

        events += count0;
        max_events -= count0;
        if (max_events == 0) {
            return count0;
        } 
    }

    struct kevent _events[64];
    count1 = sizeof(_events) / sizeof(_events[0]);
    if (count1 > max_events)
        count1 = max_events;

    struct timespec _timeout, *ptimeout;
    if (count0 > 0) {
        _timeout.tv_sec = 0;
        _timeout.tv_nsec = 0;
        ptimeout = &_timeout;
    } else if (timeout->tv_sec != -1) {
        _timeout.tv_sec = timeout->tv_sec;
        _timeout.tv_nsec = timeout->tv_usec * 1000;
        ptimeout = &_timeout;
    } else {
        ptimeout = NULL;
    }

    int ret = kevent(_p->kq, NULL, 0, _events, count1, ptimeout);
    if (ret == 0) {
        //printf("kqueue_poller_poll kevent ret=0\n");
        return count0;
    } else if (ret < 0) {
        if (errno != EINTR) {
            printf("kqueue_poller_poll kevent ret=%d,errno=%d\n", ret, errno);
            return -1;
        } else {
            return count0;
        }
    }

    count1 = 0;
    for (int i = 0; i < ret; i++) {
        nanoev_proactor *proactor = (nanoev_proactor*)_events[i].udata;
        ASSERT(proactor);
        ASSERT(proactor->reactor_cb);

        if (_events[i].filter == EVFILT_USER) {
            ASSERT(_events[i].ident == 1);
            continue;
        } 
        
        io_context *ctx = NULL;
        if (_events[i].filter == EVFILT_READ) {
            ctx = proactor->reactor_cb(proactor, _EV_READ);
        } else {
            ASSERT(_events[i].filter == EVFILT_WRITE);
            ctx = proactor->reactor_cb(proactor, _EV_WRITE);
        }
        if (ctx != NULL) {
        	events[count1].proactor = proactor;
        	events[count1].ctx = ctx;
        	count1++;
        } else {
            printf("kqueue_poller_poll reactor_cb return NULL\n");
        }
    }
    //printf("kqueue_poller_poll read %d events\n", count);
    return count0 + count1;
}

int kqueue_poller_submit(poller p, const poller_event *event)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    if (_p->events_count == _p->events_capacity) {
        void *new_events = mem_realloc(_p->events, sizeof(poller_event)*(_p->events_capacity + 128));
        if (!new_events) {
            return -1;
        }
        _p->events = new_events;
        _p->events_capacity += 128;
    }

    ASSERT(_p->events);
    ASSERT(_p->events_count < _p->events_capacity);
    memcpy(_p->events + _p->events_count, event, sizeof(poller_event));
    _p->events_count += 1;

    return 0;
}

int kqueue_poller_notify(poller p)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq > 0);

    struct kevent kev[1];
    EV_SET(kev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    int ret = kevent(_p->kq, kev, 1, NULL, 0, NULL);
    if (ret != 0) {
        printf("kqueue_poller_notify kevent ret=%d,errno=%d\n", ret, errno);
        return -1;
    }

    return 0;
}

/*----------------------------------------------------------------------------*/

poller_impl _nanoev_poller_impl = {
    .poller_create  = kqueue_poller_create,
    .poller_destroy = kqueue_poller_destroy,
    .poller_modify  = kqueue_poller_modify,
    .poller_poll    = kqueue_poller_poll,
    .poller_submit  = kqueue_poller_submit,
    .poller_notify  = kqueue_poller_notify,
};

/*----------------------------------------------------------------------------*/

#endif /* __APPLE__ */

#ifdef __APPLE__

#include "nanoev_poller.h"
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <errno.h>

/*----------------------------------------------------------------------------*/

typedef struct _kqueue_poller {
    int kq;
    poller_event *events;
    int events_start;
    int events_count;
    int events_capacity;
} _kqueue_poller;

poller kqueue_poller_create(void)
{
    _kqueue_poller *p = (_kqueue_poller*)mem_alloc(sizeof(_kqueue_poller));
    if (!p)
        return NULL;

    p->kq = kqueue();
    if (p->kq == -1) {
        mem_free(p);
        return NULL;
    }
    if (!set_close_on_exec(p->kq, 1)) {
        close(p->kq);
        mem_free(p);
        return NULL;
    }

    struct kevent kev[1];
    EV_SET(kev, 1, EVFILT_USER, EV_ADD, 0, 0, NULL);
    int ret = kevent(p->kq, kev, 1, NULL, 0, NULL);
    if (ret != 0) {
        close(p->kq);
        mem_free(p);
        return NULL;
    }

    p->events = NULL;
    p->events_start = 0;
    p->events_count = 0;
    p->events_capacity = 0;

    return p;
}

void kqueue_poller_destroy(poller p)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq >= 0);

    close(_p->kq);
    mem_free(_p->events);
    mem_free(_p);
}

int kqueue_poller_modify(poller p, SOCKET fd, nanoev_proactor *proactor, int events)
{
    struct kevent changes[2];
    int changes_count = 0;
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq >= 0);

    int reactor_events = proactor->reactor_events;
    if (reactor_events == events) {
        return 0;
    }

    if ((events & _EV_READ) && !(reactor_events & _EV_READ)) {
        EV_SET(&changes[changes_count++], fd, EVFILT_READ, EV_ADD, 0, 0, proactor);
        reactor_events |= _EV_READ;
    } else if (!(events & _EV_READ) && (reactor_events & _EV_READ)) {
        EV_SET(&changes[changes_count++], fd, EVFILT_READ, EV_DELETE, 0, 0, proactor);
        reactor_events &= ~_EV_READ;
    }

    if ((events & _EV_WRITE) && !(reactor_events & _EV_WRITE)) {
        EV_SET(&changes[changes_count++], fd, EVFILT_WRITE, EV_ADD, 0, 0, proactor);
        reactor_events |= _EV_WRITE;
    } else if (!(events & _EV_WRITE) && (reactor_events & _EV_WRITE)) {
        EV_SET(&changes[changes_count++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, proactor);
        reactor_events &= ~_EV_WRITE;
    }
    if (changes_count > 0) {
        int ret = kevent(_p->kq, changes, changes_count, NULL, 0, NULL);
        if (ret != 0) {
            return -1;
        }
        proactor->reactor_events = reactor_events;
    }

    return 0;
}

int kqueue_poller_poll(poller p, poller_event *events, int max_events, const nanoev_timeval *timeout)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq >= 0);
    int count = 0;

    if (_p->events_count > 0) {
        count = _p->events_count;
        if (count > max_events)
            count = max_events;

        memcpy(events, _p->events + _p->events_start, sizeof(poller_event)*count);
        _p->events_count -= count;
        if (_p->events_count > 0) {
            _p->events_start += count;
        } else {
            _p->events_start = 0;
        }

        return count;
    }

    struct kevent _events[256];
    count = sizeof(_events) / sizeof(_events[0]);
    if (count > max_events)
        count = max_events;

    struct timespec _timeout, *ptimeout;
    if (timeout->tv_sec != -1) {
        _timeout.tv_sec = timeout->tv_sec;
        _timeout.tv_nsec = timeout->tv_usec * 1000;
        ptimeout = &_timeout;
    } else {
        ptimeout = NULL;
    }

    int ret = kevent(_p->kq, NULL, 0, _events, count, ptimeout);
    if (ret == 0) {
        return 0;
    } else if (ret < 0) {
        if (errno != EINTR) {
            return -1;
        } else {
            return 0;
        }
    }

    count = 0;
    for (int i = 0; i < ret; i++) {
        if (_events[i].filter == EVFILT_USER) {
            ASSERT(_events[i].ident == 1);
            continue;
        } 

        nanoev_proactor *proactor = (nanoev_proactor*)_events[i].udata;
        ASSERT(proactor);
        ASSERT(proactor->reactor_cb);
        
        io_context *ctx = NULL;
        if (_events[i].filter == EVFILT_READ) {
            ctx = proactor->reactor_cb(proactor, _EV_READ);
        } else {
            ASSERT(_events[i].filter == EVFILT_WRITE);
            ctx = proactor->reactor_cb(proactor, _EV_WRITE);
        }
        if (ctx != NULL) {
            events[count].proactor = proactor;
            events[count].ctx = ctx;
            count++;
        }
    }
    return count;
}

int kqueue_poller_submit(poller p, const poller_event *event)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq >= 0);

    if (_p->events_start + _p->events_count == _p->events_capacity) {
        if (_p->events_start > 0) {
            memmove(_p->events, _p->events + _p->events_start, sizeof(poller_event)*_p->events_count);
            _p->events_start = 0;
        }
    }

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
    memcpy(_p->events + _p->events_start + _p->events_count, event, sizeof(poller_event));
    _p->events_count += 1;

    return 0;
}

int kqueue_poller_notify(poller p)
{
    _kqueue_poller *_p = (_kqueue_poller*)p;
    ASSERT(_p->kq >= 0);

    struct kevent kev[1];
    EV_SET(kev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    int ret = kevent(_p->kq, kev, 1, NULL, 0, NULL);
    if (ret != 0) {
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

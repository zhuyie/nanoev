#ifdef __linux__

#include "nanoev_poller.h"
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

/*----------------------------------------------------------------------------*/

typedef struct _epoll_poller {
    int epd;
    int notifyfd;
    poller_event *events;
    int events_start;
    int events_count;
    int events_capacity;
} _epoll_poller;

static int epoll_append_reactor_event(
    poller_event *events,
    int count,
    int max_events,
    nanoev_proactor *proactor,
    int reactor_event
    )
{
    io_context *ctx;

    if (count >= max_events)
        return count;

    ctx = proactor->reactor_cb(proactor, reactor_event);
    if (ctx != NULL) {
        events[count].proactor = proactor;
        events[count].ctx = ctx;
        count++;
    }

    return count;
}

poller epoll_poller_create(void)
{
    _epoll_poller *p = (_epoll_poller*)mem_alloc(sizeof(_epoll_poller));
    if (!p)
        return NULL;

    p->epd = epoll_create1(0);
    if (p->epd == -1) {
        mem_free(p);
        return NULL;
    }
    if (!set_close_on_exec(p->epd, 1)) {
        close(p->epd);
        mem_free(p);
        return NULL;
    }

    p->notifyfd = eventfd(0, 0);
    if (p->notifyfd != -1) {
        if (!set_close_on_exec(p->notifyfd, 1)) {
            close(p->notifyfd);
            p->notifyfd = -1;
        }
    }
    if (p->notifyfd != -1) {
        struct epoll_event event;
        event.data.fd = p->notifyfd;
        event.events = EPOLLIN;
        if (epoll_ctl(p->epd, EPOLL_CTL_ADD, p->notifyfd, &event)) {
            close(p->notifyfd);
            p->notifyfd = -1;
        }
    }
    if (p->notifyfd == -1) {
        close(p->epd);
        mem_free(p);
        return NULL;
    }

    p->events = NULL;
    p->events_start = 0;
    p->events_count = 0;
    p->events_capacity = 0;

    return p;
}

void epoll_poller_destroy(poller p)
{
    _epoll_poller *_p = (_epoll_poller*)p;
    ASSERT(_p->epd >= 0);

    close(_p->notifyfd);
    close(_p->epd);
    mem_free(_p->events);
    mem_free(_p);
}

int epoll_poller_modify(poller p, SOCKET fd, nanoev_proactor *proactor, int events)
{
    struct epoll_event event;
    int op;
    _epoll_poller *_p = (_epoll_poller*)p;
    ASSERT(_p->epd >= 0);

    if (proactor->reactor_events == events) {
        return 0;
    }

    event.data.ptr = proactor;

    event.events = 0;
    if (events & _EV_READ) {
        event.events |= EPOLLIN;
    }
    if (events & _EV_WRITE) {
        event.events |= EPOLLOUT;
    }

    if (proactor->reactor_events == 0) {
        op = EPOLL_CTL_ADD;
    } else if (events == 0) {
        op = EPOLL_CTL_DEL;
    } else {
        op = EPOLL_CTL_MOD;
    }

    int ret = epoll_ctl(_p->epd, op, fd, &event);
    if (ret != 0) {
        return -1;
    }
    proactor->reactor_events = events;

    return 0;
}

int epoll_poller_poll(poller p, poller_event *events, int max_events, const nanoev_timeval *timeout)
{
    _epoll_poller *_p = (_epoll_poller*)p;
    ASSERT(_p->epd >= 0);
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

    struct epoll_event _events[256];
    count = sizeof(_events) / sizeof(_events[0]);
    if (count > max_events)
        count = max_events;

    int timeout_in_ms;
    if (timeout->tv_sec != -1) {
        timeout_in_ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
    } else {
        timeout_in_ms = -1;
    }

    int ret = epoll_wait(_p->epd, _events, count, timeout_in_ms);
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
        if (_events[i].data.fd == _p->notifyfd) {
            uint64_t notify_count;
            read(_p->notifyfd, &notify_count, sizeof(notify_count));
            continue;
        }

        nanoev_proactor *proactor = (nanoev_proactor*)_events[i].data.ptr;
        ASSERT(proactor);
        ASSERT(proactor->reactor_cb);

        if (_events[i].events & EPOLLIN) {
            count = epoll_append_reactor_event(events, count, max_events, proactor, _EV_READ);
        } else if (_events[i].events & EPOLLOUT) {
            count = epoll_append_reactor_event(events, count, max_events, proactor, _EV_WRITE);
        } else if ((_events[i].events & (EPOLLERR | EPOLLHUP))
            && !(_events[i].events & (EPOLLIN | EPOLLOUT))) {
            if (proactor->reactor_events & _EV_READ) {
                count = epoll_append_reactor_event(events, count, max_events, proactor, _EV_READ);
            }
            if (proactor->reactor_events & _EV_WRITE) {
                count = epoll_append_reactor_event(events, count, max_events, proactor, _EV_WRITE);
            }
        }
    }
    return count;
}

int epoll_poller_submit(poller p, const poller_event *event)
{
    _epoll_poller *_p = (_epoll_poller*)p;
    ASSERT(_p->epd >= 0);

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

int epoll_poller_notify(poller p)
{
    _epoll_poller *_p = (_epoll_poller*)p;
    ASSERT(_p->epd >= 0);
    ASSERT(_p->notifyfd >= 0);

    uint64_t count = 1;
    int ret = write(_p->notifyfd, &count, sizeof(count));
    if (ret < 0) {
        return -1;
    }

    return 0;
}

/*----------------------------------------------------------------------------*/

poller_impl _nanoev_poller_impl = {
    .poller_create  = epoll_poller_create,
    .poller_destroy = epoll_poller_destroy,
    .poller_modify  = epoll_poller_modify,
    .poller_poll    = epoll_poller_poll,
    .poller_submit  = epoll_poller_submit,
    .poller_notify  = epoll_poller_notify,
};

/*----------------------------------------------------------------------------*/

#endif

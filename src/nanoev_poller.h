#ifndef __NANOEV_POLLER_H__
#define __NANOEV_POLLER_H__

#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

typedef void* poller;

typedef struct poller_event {
    nanoev_proactor *proactor;
    io_context *ctx;
} poller_event;

typedef struct poller_impl {

    poller (*poller_create)();

    void (*poller_destroy)(poller p);

    int (*poller_add)(poller p, SOCKET fd, nanoev_proactor *proactor, int events);

    int (*poller_mod)(poller p, SOCKET fd, nanoev_proactor *proactor, int events);

    int (*poller_del)(poller p, SOCKET fd);

    int (*poller_wait)(poller p, poller_event *events, int max_events, const struct nanoev_timeval *timeout);

#ifdef _WIN32
    void* (*poller_handle)(poller p);
#endif
} poller_impl;

poller_impl* get_poller_impl();

/*----------------------------------------------------------------------------*/

#endif  /* __NANOEV_POLLER_H__ */

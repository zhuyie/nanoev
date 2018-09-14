#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_async {
    NANOEV_PROACTOR_FILEDS
    OVERLAPPED overlapped;
    nanoev_async_callback on_async;
    int started;
    volatile long async_sent;
};
typedef struct nanoev_async nanoev_async;

static void __async_proactor_callback(nanoev_proactor *proactor, LPOVERLAPPED overlapped);

#define NANOEV_ASYNC_FLAG_DELETED      NANOEV_PROACTOR_FLAG_DELETED

/*----------------------------------------------------------------------------*/

nanoev_event* async_new(nanoev_loop *loop, void *userdata)
{
    nanoev_async *async;

    async = (nanoev_async*)mem_alloc(sizeof(nanoev_async));
    if (!async)
        return NULL;

    memset(async, 0, sizeof(nanoev_async));

    async->type = nanoev_event_async;
    async->loop = loop;
    async->userdata = userdata;
    async->callback = __async_proactor_callback;

    return (nanoev_event*)async;
}

void async_free(nanoev_event *event)
{
    nanoev_async *async = (nanoev_async*)event;

    if (async->started) {
        dec_outstanding_io(async->loop);
        async->started = 0;
    }

    if (InterlockedCompareExchange(&(async->async_sent), 0, 0)) {
        /* lazy delete */
        add_endgame_proactor(async->loop, (nanoev_proactor*)async);
    } else {
        mem_free(async);
    }
}

int nanoev_async_start(nanoev_event *event, nanoev_async_callback callback)
{
    nanoev_async *async = (nanoev_async*)event;

    ASSERT(async);
    ASSERT(!(async->flags & NANOEV_ASYNC_FLAG_DELETED));
    ASSERT(in_loop_thread(async->loop));
    ASSERT(callback);

    if (!async->started) {
        async->on_async = callback;
        inc_outstanding_io(async->loop);
        async->started = 1;
        return NANOEV_SUCCESS;
    } else {
        return NANOEV_ERROR_ACCESS_DENIED;
    }
}

void nanoev_async_send(nanoev_event *event)
{
    nanoev_async *async = (nanoev_async*)event;

    ASSERT(async);
    ASSERT(!(async->flags & NANOEV_ASYNC_FLAG_DELETED));

    if (0 == InterlockedCompareExchange(&(async->async_sent), 1, 0)) {
        post_fake_io(async->loop, 0, (void*)async, &async->overlapped);
    }
}

int nanoev_async_pending(nanoev_event *event)
{
    nanoev_async *async = (nanoev_async*)event;

    ASSERT(async);
    ASSERT(!(async->flags & NANOEV_ASYNC_FLAG_DELETED));

    // return async->async_sent;
    return InterlockedCompareExchange(&(async->async_sent), 0, 0);
}

/*----------------------------------------------------------------------------*/

void __async_proactor_callback(nanoev_proactor *proactor, LPOVERLAPPED overlapped)
{
    nanoev_async *async = (nanoev_async*)proactor;

    // async->async_sent = 0;
    InterlockedCompareExchange(&(async->async_sent), 0, 1);

    if (!(async->flags & NANOEV_ASYNC_FLAG_DELETED)) {
        async->on_async((nanoev_event*)async);
    }
}

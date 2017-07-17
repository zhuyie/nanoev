#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_async {
    NANOEV_PROACTOR_FILEDS
    OVERLAPPED overlapped;
    nanoev_async_callback on_async;
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

    if (async->async_sent) {
        /* lazy delete */
        add_endgame_proactor(async->loop, (nanoev_proactor*)async);
    } else {
        mem_free(async);
    }
}

void nanoev_async_start(nanoev_event *event, nanoev_async_callback callback)
{
    nanoev_async *async = (nanoev_async*)event;

    ASSERT(async);
    ASSERT(!(async->flags & NANOEV_ASYNC_FLAG_DELETED));
    ASSERT(in_loop_thread(async->loop));
    ASSERT(callback);

    async->on_async = callback;
    add_outstanding_io(async->loop);
}

void nanoev_async_send(nanoev_event *event)
{
    nanoev_async *async = (nanoev_async*)event;

    ASSERT(async);
    ASSERT(!(async->flags & NANOEV_ASYNC_FLAG_DELETED));

    if (0 == InterlockedCompareExchange(&(async->async_sent), 1, 0)) {
        post_fake_io(async->loop, 0, (ULONG_PTR)async, &async->overlapped);
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

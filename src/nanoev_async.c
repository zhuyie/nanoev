#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_async {
    NANOEV_PROACTOR_FILEDS
    io_context ctx;
    nanoev_async_callback on_async;
    int started;
    volatile long async_sent;
};
typedef struct nanoev_async nanoev_async;

static void __async_proactor_callback(nanoev_proactor *proactor, io_context *ctx);

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
    async->cb = __async_proactor_callback;

    return (nanoev_event*)async;
}

void async_free(nanoev_event *event)
{
    nanoev_async *async = (nanoev_async*)event;

    if (async->started) {
        async->started = 0;
    }

#ifdef _WIN32
    if (InterlockedCompareExchange(&(async->async_sent), 0, 0)) {
        /* lazy delete */
        add_endgame_proactor(async->loop, (nanoev_proactor*)async);
    } else {
        mem_free(async);
    }
#else
    // TODO
#endif
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

#ifdef _WIN32
    if (0 == InterlockedCompareExchange(&(async->async_sent), 1, 0)) {
        submit_fake_io(async->loop, (nanoev_proactor*)async, &async->ctx);
    }
#else
    // TODO
#endif
}

int nanoev_async_pending(nanoev_event *event)
{
    nanoev_async *async = (nanoev_async*)event;

    ASSERT(async);
    ASSERT(!(async->flags & NANOEV_ASYNC_FLAG_DELETED));

#ifdef _WIN32
    // return async->async_sent;
    return InterlockedCompareExchange(&(async->async_sent), 0, 0);
#else
    // TODO
    return 0;
#endif
}

/*----------------------------------------------------------------------------*/

void __async_proactor_callback(nanoev_proactor *proactor, io_context *ctx)
{
    nanoev_async *async = (nanoev_async*)proactor;

#ifdef _WIN32
    // async->async_sent = 0;
    InterlockedCompareExchange(&(async->async_sent), 0, 1);

    if (!(async->flags & NANOEV_ASYNC_FLAG_DELETED)) {
        async->on_async((nanoev_event*)async);
    }
#else
    // TODO    
#endif
}

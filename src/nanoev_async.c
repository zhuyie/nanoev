#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

#ifdef _WIN32
#define pipe_handle    HANDLE
#define INVALID_PIPE   NULL
#define close_pipe(p)  CloseHandle(p)
#else
# define pipe_handle   int
# define INVALID_PIPE  0
# define close_pipe(p) close(p)
#endif

struct nanoev_async {
    NANOEV_PROACTOR_FILEDS
    io_context ctx;
    nanoev_async_callback on_async;
    int started;
    pipe_handle pipe_r;
    pipe_handle pipe_w;

    mutex lock;
    int async_sent;
};
typedef struct nanoev_async nanoev_async;

static void __async_proactor_callback(nanoev_proactor *proactor, io_context *ctx);
static io_context* reactor_cb(nanoev_proactor *proactor, int events);

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
    async->reactor_cb = reactor_cb;

    mutex_init(&async->lock);

    return (nanoev_event*)async;
}

void async_free(nanoev_event *event)
{
    nanoev_async *async = (nanoev_async*)event;

    mutex_lock(&async->lock);
    if (async->async_sent) {
        /* lazy delete */
        add_endgame_proactor(async->loop, (nanoev_proactor*)async);
        mutex_unlock(&async->lock);
        return;
    }
    mutex_unlock(&async->lock);

    if (async->pipe_r != INVALID_PIPE) {
        close_pipe(async->pipe_r);
    }
    if (async->pipe_w != INVALID_PIPE) {
        close_pipe(async->pipe_w);
    }

    mutex_uninit(&async->lock);
    
    mem_free(async);
}

int nanoev_async_start(nanoev_event *event, nanoev_async_callback callback)
{
    int ret;
    nanoev_async *async = (nanoev_async*)event;

    ASSERT(async);
    ASSERT(!(async->flags & NANOEV_ASYNC_FLAG_DELETED));
    ASSERT(in_loop_thread(async->loop));
    ASSERT(callback);

    if (async->started) {
        return NANOEV_ERROR_ACCESS_DENIED;
    }

    ASSERT(async->pipe_r == INVALID_PIPE);
    ASSERT(async->pipe_w == INVALID_PIPE);
#ifdef _WIN32
    // TODO
#else
    int fildes[2] = { 0 };
    if (pipe(fildes)) {
        return NANOEV_ERROR_FAIL;
    }
    set_non_blocking(fildes[0], 1);
    set_non_blocking(fildes[1], 1);
    if (register_proactor(async->loop, (nanoev_proactor*)async, fildes[0], _EV_READ)) {
        close_pipe(fildes[0]);
        close_pipe(fildes[1]);
        return NANOEV_ERROR_FAIL;
    }
    async->pipe_r = fildes[0];
    async->pipe_w = fildes[1];
#endif

    async->on_async = callback;
    async->started = 1;
    ASSERT(async->async_sent == 0);

    return NANOEV_SUCCESS;
}

int nanoev_async_send(nanoev_event *event)
{
    int ret;
    nanoev_async *async = (nanoev_async*)event;

    ASSERT(async);
    ASSERT(!(async->flags & NANOEV_ASYNC_FLAG_DELETED));
    ASSERT(async->started);

    /* acquire the lock */
    mutex_lock(&async->lock);
    if (async->async_sent) {
        ret = NANOEV_SUCCESS;
        goto my_exit;
    }
    /* still holding the lock... */

#ifdef _WIN32
    // TODO
#else
    char buf[1] = { 0 };
    if (1 != write(async->pipe_w, buf, 1)) {
        ret = NANOEV_ERROR_FAIL;
        goto my_exit;
    }
#endif
    async->async_sent = 1;

    ret = NANOEV_SUCCESS;
my_exit:
    /* release the lock */
    mutex_unlock(&async->lock);

    return ret;
}

/*----------------------------------------------------------------------------*/

void __async_proactor_callback(nanoev_proactor *proactor, io_context *ctx)
{
    int async_sent;
    nanoev_async *async = (nanoev_async*)proactor;

    mutex_lock(&async->lock);
    async_sent = async->async_sent; 
    async->async_sent = 0;
    mutex_unlock(&async->lock);

    if (async_sent && !(async->flags & NANOEV_ASYNC_FLAG_DELETED)) {
        async->on_async((nanoev_event*)async);
    }
}

static io_context* reactor_cb(nanoev_proactor *proactor, int events)
{
#ifndef _WIN32
    nanoev_async *async = (nanoev_async*)proactor;
    ASSERT(events == _EV_READ);

    char buf[1];
    int ret = read(async->pipe_r, buf, 1);
    if (ret > 0) {
        async->ctx.status = 0;
        async->ctx.bytes = 1;
    } else {
        ASSERT(ret == -1);
        async->ctx.status = errno;
        async->ctx.bytes = 0;
    }
    return &(async->ctx);
#else
    ASSERT(!"not reached");
    return NULL;
#endif
}

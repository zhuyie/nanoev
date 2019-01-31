#include "nanoev_internal.h"
#include "nanoev_poller.h"

/*----------------------------------------------------------------------------*/

struct nanoev_loop {
    void *userdata;
    poller_impl *poller_impl_;
    poller poller_;
    int error_code;                               /* last error code */
    thread_t thread_id;                           /* thread(ID) which running the loop */
    nanoev_proactor *endgame_proactor_listhead;   /* lazy-delete proactor list */
    nanoev_timeval now;
    timer_min_heap timers;

    mutex lock;
    int is_break;
};

static void __process_endgame_proactor(nanoev_loop *loop, int enforcing);
static void __update_time(nanoev_loop *loop);

/*----------------------------------------------------------------------------*/

nanoev_loop* nanoev_loop_new(void *userdata)
{
    nanoev_loop *loop;

    loop = (nanoev_loop*)mem_alloc(sizeof(nanoev_loop));
    if (!loop)
        return NULL;

    memset(loop, 0, sizeof(nanoev_loop));

    loop->userdata = userdata;

    loop->poller_impl_ = get_poller_impl();
    ASSERT(loop->poller_impl_);

    loop->poller_ = loop->poller_impl_->poller_create();
    if (!loop->poller_) {
        mem_free(loop);
        return NULL;
    }

    timers_init(&loop->timers);

    mutex_init(&loop->lock);

    return loop;
}

void nanoev_loop_free(nanoev_loop *loop)
{
    ASSERT(loop);

    ASSERT(loop->poller_);
    loop->poller_impl_->poller_destroy(loop->poller_);

    __process_endgame_proactor(loop, 1);

    timers_term(&loop->timers);

    mutex_uninit(&loop->lock);

    mem_free(loop);
}

int nanoev_loop_run(nanoev_loop *loop)
{
    poller_event events[128];
    int count, i;
    nanoev_timeval timeout;
    int ret_code = NANOEV_SUCCESS;

    ASSERT(loop);

    /* reset is_break */
    mutex_lock(&loop->lock);
    loop->is_break = 0;
    mutex_unlock(&loop->lock);

    /* record the running thread ID */
    ASSERT(loop->thread_id == (thread_t)NULL);
    loop->thread_id = get_current_thread();

    /* make sure we have a valid time before enter into the while loop */
    nanoev_now(&loop->now);

    while (1) {
        /* update time */
        __update_time(loop);
        
        /* process timer */
        timers_process(&loop->timers, &loop->now);

        /* process lazy-delete proactor */
        __process_endgame_proactor(loop, 0);

        /* get a appropriate time-out */
        timers_timeout(&loop->timers, &loop->now, &timeout);

        /* waiting I/O events */
        count = loop->poller_impl_->poller_poll(
            loop->poller_, 
            events, 
            sizeof(events)/sizeof(events[0]), 
            &timeout);
        if (count < 0) {
            ret_code = NANOEV_ERROR_FAIL;
            break;
        }

        /* process events */
        for (i = 0; i < count; ++i) {
            events[i].proactor->cb(events[i].proactor, events[i].ctx);
        }

        /* check is_break */
        mutex_lock(&loop->lock);
        if (loop->is_break) {
            mutex_unlock(&loop->lock);
            break;
        }
        mutex_unlock(&loop->lock);
    }

    /* clear the running thread ID */
    loop->thread_id = (thread_t)NULL;

    return ret_code;
}

void nanoev_loop_break(nanoev_loop *loop)
{
    ASSERT(loop);

    mutex_lock(&loop->lock);
    if (!loop->is_break) {
        loop->is_break = 1;
        loop->poller_impl_->poller_notify(loop->poller_);
    }
    mutex_unlock(&loop->lock);
}

void* nanoev_loop_userdata(nanoev_loop *loop)
{
    ASSERT(loop);
    return loop->userdata;
}

void nanoev_loop_now(nanoev_loop *loop, nanoev_timeval *now)
{
    ASSERT(loop);
    ASSERT(now);
    if (loop->now.tv_sec || loop->now.tv_usec)
        *now = loop->now;
    else
        nanoev_now(now);
}

timer_min_heap* get_loop_timers(nanoev_loop *loop)
{
    ASSERT(loop);
    return &loop->timers;
}

/*----------------------------------------------------------------------------*/

int in_loop_thread(nanoev_loop *loop)
{
    ASSERT(loop);
    if (loop->thread_id != (thread_t)NULL && loop->thread_id != get_current_thread()) {
        return 0;
    }
    return 1;
}

int register_proactor(nanoev_loop *loop, nanoev_proactor *proactor, SOCKET sock, int events)
{
    return loop->poller_impl_->poller_modify(loop->poller_, sock, proactor, events);
}

void add_endgame_proactor(nanoev_loop *loop, nanoev_proactor *proactor)
{
    ASSERT(!(proactor->flags & NANOEV_PROACTOR_FLAG_DELETED));
    proactor->flags |= NANOEV_PROACTOR_FLAG_DELETED;
    proactor->next = loop->endgame_proactor_listhead;
    loop->endgame_proactor_listhead = proactor;
}

void submit_fake_io(nanoev_loop *loop, nanoev_proactor *proactor, io_context *ctx)
{
    poller_event event;
    event.proactor = proactor;
    event.ctx = ctx;
    loop->poller_impl_->poller_submit(loop->poller_, &event);
}

/*----------------------------------------------------------------------------*/

#define HAS_NO_OUTSTANDING_IO(proactor)                       \
    (!((proactor)->flags & NANOEV_PROACTOR_FLAG_WRITING)      \
     && !((proactor)->flags & NANOEV_PROACTOR_FLAG_READING)   \
    )

static void __remove_lazy_delete_flags(nanoev_proactor *proactor)
{
    proactor->flags &= ~NANOEV_PROACTOR_FLAG_DELETED;
    proactor->flags &= ~NANOEV_PROACTOR_FLAG_READING;
    proactor->flags &= ~NANOEV_PROACTOR_FLAG_WRITING;
}

static void __free_proactor(nanoev_proactor *proactor)
{
    switch (proactor->type) {
    case nanoev_event_tcp:
        tcp_free((nanoev_event*)proactor);
        return;

    case nanoev_event_udp:
        udp_free((nanoev_event*)proactor);
        return;

    case nanoev_event_async:
        async_free((nanoev_event*)proactor);
        return;

    default:
        ASSERT(0);
    }
}

static void __process_endgame_proactor(nanoev_loop *loop, int enforcing)
{
    nanoev_proactor **cur, *next;

    cur = &loop->endgame_proactor_listhead;
    while (*cur) {
        if (enforcing || HAS_NO_OUTSTANDING_IO(*cur)) {
            next = (*cur)->next;
            if (enforcing) {
                __remove_lazy_delete_flags(*cur);
            }
            __free_proactor(*cur);
            *cur = next;
        } else {
            cur = &((*cur)->next);
        }
    }
}

static void __update_time(nanoev_loop *loop)
{
    nanoev_timeval tv, off;

    nanoev_now(&tv);

    if (time_cmp(&tv, &loop->now) < 0) {
        off = loop->now;
        time_sub(&off, &tv);
        timers_adjust_backward(&loop->timers, &off);
    }

    loop->now = tv;
}

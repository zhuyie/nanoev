#include "nanoev_internal.h"
#include "nanoev_poller.h"

/*----------------------------------------------------------------------------*/

struct nanoev_loop {
    void *userdata;
    poller_impl *poller_impl_;
    poller poller_;
    int error_code;                               /* last error code */
    void* thread_id;                              /* thread(ID) which running the loop */
    nanoev_proactor *endgame_proactor_listhead;   /* lazy-delete proactor list */
    long outstanding_io_count;
    nanoev_timeval now;
    timer_min_heap timers;
};

static void __process_endgame_proactor(nanoev_loop *loop, int enforcing);
static void __update_time(nanoev_loop *loop);
static nanoev_proactor* loop_break_key = (nanoev_proactor*)-1;

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

    return loop;
}

void nanoev_loop_free(nanoev_loop *loop)
{
    ASSERT(loop);

    ASSERT(loop->poller_);
    loop->poller_impl_->poller_destroy(loop->poller_);

    __process_endgame_proactor(loop, 1);

    timers_term(&loop->timers);

    mem_free(loop);
}

int nanoev_loop_run(nanoev_loop *loop)
{
    poller_event events[128];
    int count, i;
    nanoev_timeval timeout;
    int ret_code = NANOEV_SUCCESS;
    int do_break = 0;

    ASSERT(loop);

    /* record the running thread ID */
    ASSERT(loop->thread_id == NULL);
#ifdef _WIN32
    loop->thread_id = (void*)GetCurrentThreadId();
#else
    // TODO
#endif

    /* make sure we have a valid time before enter into the while loop */
    nanoev_now(&loop->now);

    while (loop->timers.size || loop->outstanding_io_count) {
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
            if (events[i].proactor == loop_break_key) {
                dec_outstanding_io(loop);
                do_break = 1;
            } else {
                events[i].proactor->cb(events[i].proactor, events[i].ctx);
                dec_outstanding_io(loop);
            }
        }
        if (do_break) {
            goto ON_LOOP_BREAK;
        }
    }

ON_LOOP_BREAK:
    /* clear the running thread ID */
    loop->thread_id = NULL;

    return ret_code;
}

void nanoev_loop_break(nanoev_loop *loop)
{
    ASSERT(loop);
    post_fake_io(loop, loop_break_key, NULL);
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

    if (loop->thread_id != NULL) {
#ifdef _WIN32        
        if ((DWORD)(loop->thread_id) != GetCurrentThreadId()) {
            return 0;
        }
#else
        // TODO
#endif
    }

    return 1;
}

int register_proactor_to_loop(nanoev_proactor *proactor, SOCKET sock, int events, nanoev_loop *loop)
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

void inc_outstanding_io(nanoev_loop *loop)
{
    ASSERT(loop->outstanding_io_count >= 0);
#ifdef _WIN32
    InterlockedIncrement(&loop->outstanding_io_count);
#else
    loop->outstanding_io_count += 1;
#endif
}

void dec_outstanding_io(nanoev_loop *loop)
{
    ASSERT(loop->outstanding_io_count > 0);
#ifdef _WIN32
    InterlockedDecrement(&loop->outstanding_io_count);
#else
    loop->outstanding_io_count -= 1;
#endif
}

void post_fake_io(nanoev_loop *loop, nanoev_proactor *proactor, io_context *ctx)
{
    inc_outstanding_io(loop);

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

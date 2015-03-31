#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_loop {
    void *userdata;
    HANDLE iocp;                                  /* IOCP handle */
    int error_code;                               /* last error code */
    DWORD thread_id;                              /* thread(ID) which running the loop */
    nanoev_proactor *endgame_proactor_listhead;   /* lazy-delete proactor list */
    int outstanding_io_count;
    struct nanoev_timeval now;
    timer_min_heap timers;
};

static void __process_endgame_proactor(nanoev_loop *loop, int enforcing);
static void __update_time(nanoev_loop *loop);
static const ULONG_PTR loop_break_key = (ULONG_PTR)-1;

/*----------------------------------------------------------------------------*/

nanoev_loop* nanoev_loop_new(void *userdata)
{
    nanoev_loop *loop;

    loop = (nanoev_loop*)mem_alloc(sizeof(nanoev_loop));
    if (!loop)
        return NULL;

    memset(loop, 0, sizeof(nanoev_loop));

    loop->userdata = userdata;

    loop->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)0, 0);
    if (!loop->iocp) {
        mem_free(loop);
        return NULL;
    }

    timers_init(&loop->timers);

    return loop;
}

void nanoev_loop_free(nanoev_loop *loop)
{
    ASSERT(loop);

    ASSERT(loop->iocp);
    CloseHandle(loop->iocp);

    __process_endgame_proactor(loop, 1);

    timers_term(&loop->timers);

    mem_free(loop);
}

int nanoev_loop_run(nanoev_loop *loop)
{
    unsigned int timeout;
    nanoev_proactor *proactor;
    DWORD bytes;
    ULONG_PTR key;
    OVERLAPPED *overlapped;
    int ret_code = NANOEV_SUCCESS;

    ASSERT(loop);
    ASSERT(loop->iocp);

    /* record the running thread ID */
    ASSERT(loop->thread_id == 0);
    loop->thread_id = GetCurrentThreadId();

    /* make sure we have a valid time before enter into the while loop */
    nanoev_now(&loop->now);

    while (loop->timers.size || loop->outstanding_io_count) {
        /* update time */
        __update_time(loop);

        /* process lazy-delete proactor */
        __process_endgame_proactor(loop, 0);

        /* get a appropriate time-out */
        timeout = timers_timeout(&loop->timers, &loop->now);

        /* try to dequeue a completion package */
        overlapped = NULL;
        GetQueuedCompletionStatus(loop->iocp, &bytes, &key, &overlapped, timeout);
        if (overlapped) {
            /* process the completion packet */
            ASSERT(key);
            proactor = (nanoev_proactor*)key;
            proactor->callback(proactor, overlapped);

            ASSERT(loop->outstanding_io_count >= 0);
            loop->outstanding_io_count--;

        } else {
            if (loop_break_key == key) {
                /* someone called nanoev_loop_break */
                break;
            }
            if (WAIT_TIMEOUT != GetLastError()) {
                /* unexpected error */
                loop->error_code = GetLastError();
                ret_code = NANOEV_ERROR_FAIL;
                break;
            }
        }

        /* process timer */
        timers_process(&loop->timers, &loop->now);
    }

    /* clear the running thread ID */
    loop->thread_id = 0;

    return ret_code;
}

void nanoev_loop_break(nanoev_loop *loop)
{
    ASSERT(loop);
    ASSERT(loop->iocp);
    post_fake_io(loop, 0, loop_break_key, NULL);
}

void* nanoev_loop_userdata(nanoev_loop *loop)
{
    ASSERT(loop);
    return loop->userdata;
}

void nanoev_loop_now(nanoev_loop *loop, struct nanoev_timeval *now)
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

    if (loop->thread_id && GetCurrentThreadId() != loop->thread_id)
        return 0;

    return 1;
}

int register_proactor_to_loop(nanoev_proactor *proactor, SOCKET sock, nanoev_loop *loop)
{
    HANDLE ret = CreateIoCompletionPort((HANDLE)sock, loop->iocp, (ULONG_PTR)proactor, 0);
    return (ret == NULL) ? -1 : 0;
}

void add_endgame_proactor(nanoev_loop *loop, nanoev_proactor *proactor)
{
    proactor->flags |= NANOEV_PROACTOR_FLAG_DELETED;
    proactor->next = loop->endgame_proactor_listhead;
    loop->endgame_proactor_listhead = proactor;
}

void add_outstanding_io(nanoev_loop *loop)
{
    ASSERT(loop->outstanding_io_count >= 0);
    loop->outstanding_io_count++;
}

void post_fake_io(nanoev_loop *loop, DWORD cb, ULONG_PTR key, LPOVERLAPPED overlapped)
{
    PostQueuedCompletionStatus(loop->iocp, cb, key, overlapped);
}

/*----------------------------------------------------------------------------*/

#define HAS_NO_OUTSTANDING_IO(proactor)                       \
    (!((proactor)->flags & NANOEV_PROACTOR_FLAG_WRITING)      \
     && !((proactor)->flags & NANOEV_PROACTOR_FLAG_READING)   \
    )

static void __process_endgame_proactor(nanoev_loop *loop, int enforcing)
{
    nanoev_proactor **cur, *next;

    cur = &loop->endgame_proactor_listhead;
    while (*cur) {
        if (enforcing || HAS_NO_OUTSTANDING_IO(*cur)) {
            next = (*cur)->next;
            free(*cur);
            *cur = next;
        } else {
            cur = &((*cur)->next);
        }
    }
}

static void __update_time(nanoev_loop *loop)
{
    struct nanoev_timeval tv, off;

    nanoev_now(&tv);

    /* timer内部记录的timeout是基于它所设置那个时刻的系统时间来计算的。
       如果用户修改了系统时间，则timer的触发将不正确。
       我们能够检测出系统时间回退了，但若系统时间向前调了，暂时没法子处理。
     */
    if (time_cmp(&tv, &loop->now) < 0) {
        off = loop->now;
        time_sub(&off, &tv);
        timers_adjust_backward(&loop->timers, &off);
    }

    loop->now = tv;
}

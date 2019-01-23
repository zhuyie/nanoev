#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_loop {
    void *userdata;
#ifdef _WIN32
    HANDLE iocp;                                  /* IOCP handle */
#endif
    int error_code;                               /* last error code */
    void* thread_id;                              /* thread(ID) which running the loop */
    nanoev_proactor *endgame_proactor_listhead;   /* lazy-delete proactor list */
    long outstanding_io_count;
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

#ifdef _WIN32
    loop->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)0, 0);
    if (!loop->iocp) {
        mem_free(loop);
        return NULL;
    }
#endif

    timers_init(&loop->timers);

    return loop;
}

void nanoev_loop_free(nanoev_loop *loop)
{
    ASSERT(loop);

#ifdef _WIN32
    ASSERT(loop->iocp);
    CloseHandle(loop->iocp);
#endif

    __process_endgame_proactor(loop, 1);

    timers_term(&loop->timers);

    mem_free(loop);
}

int nanoev_loop_run(nanoev_loop *loop)
{
    unsigned int timeout;
    nanoev_proactor *proactor;
#ifdef _WIN32
    OVERLAPPED_ENTRY overlappeds[128];
    BOOL success;
    DWORD i, count;
#endif
    int ret_code = NANOEV_SUCCESS;

    ASSERT(loop);
    ASSERT(loop->iocp);

    /* record the running thread ID */
    ASSERT(loop->thread_id == 0);
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
        timeout = timers_timeout(&loop->timers, &loop->now);

#ifdef _WIN32
        /* try to dequeue a completion package */
        if (get_win32_ext_fns()->pGetQueuedCompletionStatusEx) {
            success = get_win32_ext_fns()->pGetQueuedCompletionStatusEx(
                loop->iocp,
                overlappeds,
                sizeof(overlappeds) / sizeof(overlappeds[0]),
                &count,
                timeout,
                FALSE
                );
        } else {
            count = 1;
            success = GetQueuedCompletionStatus(
                loop->iocp, 
                &(overlappeds[0].dwNumberOfBytesTransferred), 
                &(overlappeds[0].lpCompletionKey), 
                &(overlappeds[0].lpOverlapped), 
                timeout
                );
            if (!success) {
                /* If *lpOverlapped is not NULL, the function dequeues a completion packet 
                   for a failed I/O operation from the completion port. */
                if (overlappeds[0].lpOverlapped != NULL) {
                    success = 1;
                }
            }
        }
        if (success) {
            for (i = 0; i < count; ++i) {
                if (overlappeds[i].lpOverlapped) {
                    /* process the completion packet */
                    ASSERT(overlappeds[i].lpCompletionKey);
                    proactor = (nanoev_proactor*)overlappeds[i].lpCompletionKey;
                    proactor->callback(proactor, overlappeds[i].lpOverlapped);

                    dec_outstanding_io(loop);

                } else {
                    /* someone called nanoev_loop_break */
                    ASSERT(loop_break_key == overlappeds[i].lpCompletionKey);
                    
                    dec_outstanding_io(loop);

                    goto ON_LOOP_BREAK;
                }
            }

        } else if (WAIT_TIMEOUT != GetLastError()) {
            /* unexpected error */
            loop->error_code = GetLastError();
            ret_code = NANOEV_ERROR_FAIL;
            break;
        }
#endif
    }

ON_LOOP_BREAK:
    /* clear the running thread ID */
    loop->thread_id = NULL;

    return ret_code;
}

void nanoev_loop_break(nanoev_loop *loop)
{
    ASSERT(loop);
    ASSERT(loop->iocp);
    post_fake_io(loop, 0, (void*)loop_break_key, NULL);
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

int register_proactor_to_loop(nanoev_proactor *proactor, SOCKET sock, nanoev_loop *loop)
{
#ifdef _WIN32
    HANDLE ret = CreateIoCompletionPort((HANDLE)sock, loop->iocp, (ULONG_PTR)proactor, 0);
    return (ret == NULL) ? -1 : 0;
#else
    // TODO
#endif
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
    // TODO
#endif
}

void dec_outstanding_io(nanoev_loop *loop)
{
    ASSERT(loop->outstanding_io_count > 0);
#ifdef _WIN32
    InterlockedDecrement(&loop->outstanding_io_count);
#else
    // TODO
#endif
}

void post_fake_io(nanoev_loop *loop, unsigned int cb, void *key, LPOVERLAPPED overlapped)
{
#ifdef _WIN32
    inc_outstanding_io(loop);
    PostQueuedCompletionStatus(loop->iocp, (DWORD)cb, (ULONG_PTR)key, overlapped);
#else
    // TODO
#endif
}

/*----------------------------------------------------------------------------*/

#define HAS_NO_OUTSTANDING_IO(proactor)                       \
    (!((proactor)->flags & NANOEV_PROACTOR_FLAG_WRITING)      \
     && !((proactor)->flags & NANOEV_PROACTOR_FLAG_READING)   \
    )

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
            __free_proactor(*cur);
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

    if (time_cmp(&tv, &loop->now) < 0) {
        off = loop->now;
        time_sub(&off, &tv);
        timers_adjust_backward(&loop->timers, &off);
    }

    loop->now = tv;
}

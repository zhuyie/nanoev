#ifdef _WIN32

#include "nanoev_poller.h"

/*----------------------------------------------------------------------------*/

typedef struct _iocp_poller {
    HANDLE iocp;
} _iocp_poller;

poller iocp_poller_create()
{
    _iocp_poller *p = (_iocp_poller*)mem_alloc(sizeof(_iocp_poller));
    if (!p)
        return NULL;

    p->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)0, 0);
    if (!p->iocp) {
        mem_free(p);
        return NULL;
    }

    return p;
}

void iocp_poller_destroy(poller p)
{
    _iocp_poller *_p = (_iocp_poller*)p;
    ASSERT(_p->iocp);

    CloseHandle(_p->iocp);
    mem_free(_p);
}

int iocp_poller_add(poller p, SOCKET fd, nanoev_proactor *proactor, int events)
{
    _iocp_poller *_p = (_iocp_poller*)p;
    ASSERT(_p->iocp);

    HANDLE ret = CreateIoCompletionPort((HANDLE)fd, _p->iocp, (ULONG_PTR)proactor, 0);
    return (ret == NULL) ? -1 : 0;
}

int iocp_poller_mod(poller p, SOCKET fd, nanoev_proactor *proactor, int events)
{
    return 0;
}

int iocp_poller_del(poller p, SOCKET fd)
{
    return 0;
}

int iocp_poller_wait(poller p, poller_event *events, int max_events, const struct nanoev_timeval *timeout)
{
    OVERLAPPED_ENTRY overlappeds[128];
    BOOL success;
    DWORD i, count;
    unsigned int timeout_in_ms;
    _iocp_poller *_p = (_iocp_poller*)p;
    ASSERT(_p->iocp);

    ASSERT(events);
    ASSERT(max_events);
    ASSERT(timeout);

    count = sizeof(overlappeds) / sizeof(overlappeds[0]);
    if ((int)count > max_events)
        count = max_events;

    if (timeout->tv_sec != -1) {
        timeout_in_ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
    } else {
        timeout_in_ms = INFINITE;
    }

    /* try to dequeue a completion package */
    if (get_win32_ext_fns()->pGetQueuedCompletionStatusEx) {
        success = get_win32_ext_fns()->pGetQueuedCompletionStatusEx(
            _p->iocp,
            overlappeds,
            count,
            &count,
            timeout_in_ms,
            FALSE
            );
    } else {
        count = 1;
        success = GetQueuedCompletionStatus(
            _p->iocp, 
            &(overlappeds[0].dwNumberOfBytesTransferred), 
            &(overlappeds[0].lpCompletionKey), 
            &(overlappeds[0].lpOverlapped), 
            timeout_in_ms
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
            ASSERT(overlappeds[i].lpCompletionKey);
            events[i].proactor = (nanoev_proactor*)overlappeds[i].lpCompletionKey;
            events[i].ctx = (io_context*)overlappeds[i].lpOverlapped;
        }
        return count;
    }

    if (WAIT_TIMEOUT == GetLastError()) {
        return 0;
    } else {
        return -1;
    }
}

void* iocp_poller_handle(poller p)
{
    _iocp_poller *_p = (_iocp_poller*)p;
    return _p->iocp;
}

/*----------------------------------------------------------------------------*/

poller_impl _nanoev_poller_impl;

void init_iocp_poller_impl()
{
    _nanoev_poller_impl.poller_create  = iocp_poller_create;
    _nanoev_poller_impl.poller_destroy = iocp_poller_destroy;
    _nanoev_poller_impl.poller_add     = iocp_poller_add;
    _nanoev_poller_impl.poller_mod     = iocp_poller_mod;
    _nanoev_poller_impl.poller_del     = iocp_poller_del;
    _nanoev_poller_impl.poller_wait    = iocp_poller_wait;
    _nanoev_poller_impl.poller_handle  = iocp_poller_handle;
}

/*----------------------------------------------------------------------------*/

#endif /* _WIN32 */

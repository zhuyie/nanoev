#ifndef __NANOEV_INTERNAL_H__
#define __NANOEV_INTERNAL_H__

#include "nanoev.h"

#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>

#include <assert.h>
#define ASSERT assert

/*----------------------------------------------------------------------------*/

void* mem_alloc(size_t sz);
void* mem_realloc(void *mem, size_t sz);
void  mem_free(void *mem);

/*----------------------------------------------------------------------------*/

typedef struct {
    LPFN_CONNECTEX ConnectEx;
    LPFN_ACCEPTEX AcceptEx;
} nanoev_winsock_ext;

const nanoev_winsock_ext* get_winsock_ext();

#if _WIN32_WINNT < 0x0600

#define FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 0x1
#define FILE_SKIP_SET_EVENT_ON_HANDLE        0x2

typedef struct _OVERLAPPED_ENTRY {
    ULONG_PTR lpCompletionKey;
    LPOVERLAPPED lpOverlapped;
    ULONG_PTR Internal;
    DWORD dwNumberOfBytesTransferred;
} OVERLAPPED_ENTRY, *LPOVERLAPPED_ENTRY;

#endif

typedef BOOL (WINAPI *PFN_GetQueuedCompletionStatusEx)(
    HANDLE CompletionPort,
    LPOVERLAPPED_ENTRY lpCompletionPortEntries,
    ULONG ulCount,
    PULONG ulNumEntriesRemoved,
    DWORD dwMilliseconds,
    BOOL fAlertable
    );
typedef BOOL (WINAPI *PFN_SetFileCompletionNotificationModes)(
    HANDLE FileHandle,
    UCHAR Flags
    );

typedef struct {
    PFN_GetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx;
    PFN_SetFileCompletionNotificationModes pSetFileCompletionNotificationModes;
} nanoev_win32_ext_fns;

const nanoev_win32_ext_fns* get_win32_ext_fns();

/*----------------------------------------------------------------------------*/

#define NANOEV_EVENT_FILEDS                                  \
    nanoev_event_type type;                                  \
    unsigned int flags;                                      \
    nanoev_loop *loop;                                       \
    void *userdata;                                          \

struct nanoev_event {
    NANOEV_EVENT_FILEDS
};

nanoev_event* tcp_new(nanoev_loop *loop, void *userdata);
void tcp_free(nanoev_event *event);

nanoev_event* udp_new(nanoev_loop *loop, void *userdata);
void udp_free(nanoev_event *event);

nanoev_event* async_new(nanoev_loop *loop, void *userdata);
void async_free(nanoev_event *event);

nanoev_event* timer_new(nanoev_loop *loop, void *userdata);
void timer_free(nanoev_event *event);

/*----------------------------------------------------------------------------*/

struct nanoev_proactor;
typedef struct nanoev_proactor nanoev_proactor;

typedef void (*proactor_callback)(nanoev_proactor *proactor, LPOVERLAPPED overlapped);

#define NANOEV_PROACTOR_FILEDS                               \
    NANOEV_EVENT_FILEDS                                      \
    proactor_callback callback;                              \
    nanoev_proactor *next;                                   \

struct nanoev_proactor {
    NANOEV_PROACTOR_FILEDS
};

#define NANOEV_PROACTOR_FLAG_WRITING    (0x10000000) /* connecting or sending */
#define NANOEV_PROACTOR_FLAG_READING    (0x20000000) /* accepting or receiving */
#define NANOEV_PROACTOR_FLAG_ERROR      (0x40000000) /* something goes wrong */
#define NANOEV_PROACTOR_FLAG_DELETED    (0x80000000) /* mark for delete */

int  in_loop_thread(nanoev_loop *loop);
int  register_proactor_to_loop(nanoev_proactor *proactor, SOCKET sock, nanoev_loop *loop);
void add_endgame_proactor(nanoev_loop *loop, nanoev_proactor *proactor);
void add_outstanding_io(nanoev_loop *loop);
void post_fake_io(nanoev_loop *loop, DWORD cb, ULONG_PTR key, LPOVERLAPPED overlapped);

/*----------------------------------------------------------------------------*/

typedef struct timer_min_heap {
    nanoev_event **events;
    unsigned int capacity;
    unsigned int size;
} timer_min_heap;

void timers_init(timer_min_heap *heap);
void timers_term(timer_min_heap *heap);
unsigned int timers_timeout(timer_min_heap *heap, const struct nanoev_timeval *now);
void timers_process(timer_min_heap *heap, const struct nanoev_timeval *now);
void timers_adjust_backward(timer_min_heap *heap, const struct nanoev_timeval *off);

timer_min_heap* get_loop_timers(nanoev_loop *loop);

void time_add(struct nanoev_timeval *tv, const struct nanoev_timeval *add);
void time_sub(struct nanoev_timeval *tv, const struct nanoev_timeval *sub);
int  time_cmp(const struct nanoev_timeval *tv0, const struct nanoev_timeval *tv1);

/*----------------------------------------------------------------------------*/

int  ntstatus_to_winsock_error(long status);

/*----------------------------------------------------------------------------*/

#endif  /* __NANOEV_INTERNAL_H__ */

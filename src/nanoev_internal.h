#ifndef __NANOEV_INTERNAL_H__
#define __NANOEV_INTERNAL_H__

#include "nanoev.h"

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

typedef struct min_heap {
    nanoev_event **events;
    int capacity;
    int size;
} min_heap;

void min_heap_free(min_heap *h);
int min_heap_reserve(min_heap *h, int capacity);
int min_heap_erase(min_heap *h, nanoev_event *event);
nanoev_event* min_heap_top(min_heap *h);

/*----------------------------------------------------------------------------*/

int  ntstatus_to_winsock_error(long status);

/*----------------------------------------------------------------------------*/

#endif  /* __NANOEV_INTERNAL_H__ */

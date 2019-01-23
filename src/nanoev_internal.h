#ifndef __NANOEV_INTERNAL_H__
#define __NANOEV_INTERNAL_H__

#include "nanoev.h"

#ifdef _WIN32
# include "nanoev_internal_windows.h"
#else
# include "nanoev_internal_unix.h"
#endif

#include <assert.h>
#define ASSERT assert

/*----------------------------------------------------------------------------*/

int  global_init();
void global_term();

/*----------------------------------------------------------------------------*/

void* mem_alloc(size_t sz);
void* mem_realloc(void *mem, size_t sz);
void  mem_free(void *mem);

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

void time_now(struct nanoev_timeval *tv);
void time_add(struct nanoev_timeval *tv, const struct nanoev_timeval *add);
void time_sub(struct nanoev_timeval *tv, const struct nanoev_timeval *sub);
int  time_cmp(const struct nanoev_timeval *tv0, const struct nanoev_timeval *tv1);

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
void inc_outstanding_io(nanoev_loop *loop);
void dec_outstanding_io(nanoev_loop *loop);
void post_fake_io(nanoev_loop *loop, unsigned int cb, void *key, LPOVERLAPPED overlapped);

int  set_non_blocking(SOCKET sock, int blocking);
void close_socket(SOCKET sock);
int  socket_last_error();

/*----------------------------------------------------------------------------*/

#endif  /* __NANOEV_INTERNAL_H__ */

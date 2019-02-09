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

#ifdef _WIN32
# define mutex CRITICAL_SECTION
#else
# define mutex pthread_mutex_t
#endif

int  mutex_init(mutex *m);
void mutex_uninit(mutex *m);
void mutex_lock(mutex *m);
void mutex_unlock(mutex *m);

/*----------------------------------------------------------------------------*/

#ifdef _WIN32
# define thread_t DWORD
#else
# define thread_t pthread_t
#endif

thread_t get_current_thread();

/*----------------------------------------------------------------------------*/

typedef struct timer_min_heap {
    nanoev_event **events;
    unsigned int capacity;
    unsigned int size;
} timer_min_heap;

void timers_init(timer_min_heap *heap);
void timers_term(timer_min_heap *heap);
void timers_timeout(timer_min_heap *heap, const nanoev_timeval *now, nanoev_timeval *timeout);
void timers_process(timer_min_heap *heap, const nanoev_timeval *now);
void timers_adjust_backward(timer_min_heap *heap, const nanoev_timeval *off);

timer_min_heap* get_loop_timers(nanoev_loop *loop);

void time_now(nanoev_timeval *tv);
void time_add(nanoev_timeval *tv, const nanoev_timeval *add);
void time_sub(nanoev_timeval *tv, const nanoev_timeval *sub);
int  time_cmp(const nanoev_timeval *tv0, const nanoev_timeval *tv1);

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

typedef void (*proactor_callback)(nanoev_proactor *proactor, io_context *ctx);

#define _EV_READ     0x1
#define _EV_WRITE    0x2
#define _EV_ERROR    0x80

typedef io_context* (*reactor_event_cb)(nanoev_proactor *proactor, int events);

#define NANOEV_PROACTOR_FILEDS                               \
    NANOEV_EVENT_FILEDS                                      \
    proactor_callback cb;                                    \
    reactor_event_cb reactor_cb;                             \
    int reactor_events;                                      \
    nanoev_proactor *next;                                   \

struct nanoev_proactor {
    NANOEV_PROACTOR_FILEDS
};

#define NANOEV_PROACTOR_FLAG_WRITING    (0x10000000) /* connecting or sending */
#define NANOEV_PROACTOR_FLAG_READING    (0x20000000) /* accepting or receiving */
#define NANOEV_PROACTOR_FLAG_ERROR      (0x40000000) /* something goes wrong */
#define NANOEV_PROACTOR_FLAG_DELETED    (0x80000000) /* mark for delete */

int  in_loop_thread(nanoev_loop *loop);
int  register_proactor(nanoev_loop *loop, nanoev_proactor *proactor, SOCKET sock, int events);
void add_endgame_proactor(nanoev_loop *loop, nanoev_proactor *proactor);
void submit_fake_io(nanoev_loop *loop, nanoev_proactor *proactor, io_context *ctx);

int  set_non_blocking(SOCKET sock, int set);
void close_socket(SOCKET sock);
int  socket_last_error();

/*----------------------------------------------------------------------------*/

#endif  /* __NANOEV_INTERNAL_H__ */

#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_timer {
    NANOEV_EVENT_FILEDS
    struct nanoev_timeval timeout;
    nanoev_timer_callback callback;
    unsigned int min_heap_idx;
};
typedef struct nanoev_timer nanoev_timer;

static int min_heap_reserve(timer_min_heap *h, unsigned int capacity);
static void min_heap_erase(timer_min_heap *h, nanoev_timer *t);
static void min_heap_shift_up(timer_min_heap *h, unsigned int hole_index, nanoev_timer *t);
static void min_heap_shift_down(timer_min_heap *h, unsigned int hole_index, nanoev_timer *t);

/*----------------------------------------------------------------------------*/

nanoev_event* timer_new(nanoev_loop *loop, void *userdata)
{
    nanoev_timer *timer;

    timer = (nanoev_timer*)mem_alloc(sizeof(nanoev_timer));
    if (!timer)
        return NULL;

    memset(timer, 0, sizeof(nanoev_timer));

    timer->type = nanoev_event_timer;
    timer->loop = loop;
    timer->userdata = userdata;
    timer->min_heap_idx = (unsigned int)-1;

    return (nanoev_event*)timer;
}

void timer_free(nanoev_event *event)
{
    nanoev_timer *timer = (nanoev_timer*)event;
    
    timer_min_heap *heap = get_loop_timers(timer->loop);
    min_heap_erase(heap, timer);

    mem_free(timer);
}

int nanoev_timer_add(
    nanoev_event *event,
    struct nanoev_timeval after,
    nanoev_timer_callback callback
    )
{
    nanoev_timer *timer = (nanoev_timer*)event;
    timer_min_heap *heap;
    int ret_code;

    ASSERT(timer);
    ASSERT(in_loop_thread(timer->loop));
    ASSERT(timer->min_heap_idx == (unsigned int)-1);
    ASSERT(callback);

    nanoev_loop_now(timer->loop, &timer->timeout);
    time_add(&timer->timeout, &after);
    timer->callback = callback;

    heap = get_loop_timers(timer->loop);
    ASSERT(heap);
    ret_code = min_heap_reserve(heap, heap->size + 1);
    if (ret_code != NANOEV_SUCCESS)
        return ret_code;

    min_heap_shift_up(heap, heap->size, timer);
    heap->size++;
    
    return NANOEV_SUCCESS;
}

int nanoev_timer_del(
    nanoev_event *event
    )
{
    nanoev_timer *timer = (nanoev_timer*)event;
    timer_min_heap *heap;

    ASSERT(timer);
    ASSERT(in_loop_thread(timer->loop));
    ASSERT(timer->min_heap_idx != (unsigned int)-1);

    heap = get_loop_timers(timer->loop);
    ASSERT(heap);
    min_heap_erase(heap, timer);

    return NANOEV_SUCCESS;
}

/*----------------------------------------------------------------------------*/

void timers_init(timer_min_heap *h)
{
    ASSERT(h);
    h->events = NULL;
    h->capacity = 0;
    h->size = 0;
}

void timers_term(timer_min_heap *h)
{
    ASSERT(h);
    mem_free(h->events);
}

unsigned int timers_timeout(timer_min_heap *h, const struct nanoev_timeval *now)
{
    nanoev_timer *top;
    struct nanoev_timeval t;

    if (!h->size)
        return 0xffffffff;

    top = (nanoev_timer*)h->events[0];
    t = top->timeout;
    if (time_cmp(&t, now) <= 0)
        return 0;

    time_sub(&t, now);
    return t.tv_sec * 1000 + t.tv_usec / 1000;
}

int timers_process(timer_min_heap *h, const struct nanoev_timeval *now)
{
    nanoev_timer *top;

    while (h->size) {
        top = (nanoev_timer*)h->events[0];
        if (time_cmp(&top->timeout, now) > 0)
            break;

        top->callback((nanoev_event*)top);

        min_heap_erase(h, top);
    }

    return NANOEV_SUCCESS;
}

/*----------------------------------------------------------------------------*/

static int time_greater(nanoev_timer *t0, nanoev_timer *t1)
{
    int ret_code = time_cmp(&t0->timeout, &t1->timeout);
    return ret_code > 0 ? 1 : 0;
}

static int min_heap_reserve(timer_min_heap *h, unsigned int capacity)
{
    ASSERT(h);
    ASSERT(capacity);

    if (h->capacity < capacity) {
        nanoev_event** events_new = (nanoev_event**)mem_realloc(
            h->events, sizeof(nanoev_event*) * capacity);
        if (!events_new)
            return NANOEV_ERROR_OUT_OF_MEMORY;
        h->events = events_new;
        h->capacity = capacity;
    }

    return NANOEV_SUCCESS;
}

static void min_heap_erase(timer_min_heap *h, nanoev_timer *t)
{
    nanoev_timer *last;
    unsigned int parent;

    if (t->min_heap_idx != (unsigned int)-1) {
        /* 容量减1 */
        h->size--;
        
        /* 假定最后一个节点是在t所在的位置上（覆盖了t），然后调整堆 */
        last = (nanoev_timer*)h->events[h->size];
        parent = (t->min_heap_idx - 1) / 2;
        if (t->min_heap_idx > 0 && time_greater((nanoev_timer*)h->events[parent], last))
            min_heap_shift_up(h, t->min_heap_idx, last);
        else
            min_heap_shift_down(h, t->min_heap_idx, last);

        /* 清空t在heap中的index */
        t->min_heap_idx = (unsigned int)-1;
    }
}

static void min_heap_shift_up(timer_min_heap *h, unsigned int hole_index, nanoev_timer *t)
{
    unsigned int parent = (hole_index - 1) / 2;
    while (hole_index && time_greater((nanoev_timer*)h->events[parent], t)) {
        /* parent的timeout比t的timeout值要大，将parent挪到hole_index */
        h->events[hole_index] = h->events[parent];
        ((nanoev_timer*)h->events[hole_index])->min_heap_idx = hole_index;
        hole_index = parent;
        parent = (hole_index - 1) / 2;
    }
    /* 现在的hole_index就是t应该在的位置 */
    h->events[hole_index] = (nanoev_event*)t;
    t->min_heap_idx = hole_index;
}

static void min_heap_shift_down(timer_min_heap *h, unsigned int hole_index, nanoev_timer *t)
{
    unsigned int min_child = 2 * (hole_index + 1);
    while (min_child <= h->size) {
        if (min_child == h->size
            || time_greater((nanoev_timer*)h->events[min_child], (nanoev_timer*)h->events[min_child - 1])
            ) {
            min_child--;
        }
        if (!time_greater(t, (nanoev_timer*)h->events[min_child])) {
            break;
        }
        h->events[hole_index] = h->events[min_child];
        ((nanoev_timer*)h->events[hole_index])->min_heap_idx = min_child;
        min_child = 2 * (hole_index + 1);
    }
    /* 现在的hole_index就是t应该在的位置 */
    h->events[hole_index] = (nanoev_event*)t;
    t->min_heap_idx = hole_index;
}

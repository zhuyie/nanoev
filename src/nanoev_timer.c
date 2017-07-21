#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_timer {
    NANOEV_EVENT_FILEDS
    struct nanoev_timeval after;
    int repeat;
    unsigned int min_heap_idx;
    struct nanoev_timeval timeout;
    nanoev_timer_callback callback;
};
typedef struct nanoev_timer nanoev_timer;

static int min_heap_insert(timer_min_heap *heap, nanoev_timer *timer);
static void min_heap_erase(timer_min_heap *heap, nanoev_timer *timer);
static int min_heap_reserve(timer_min_heap *heap, unsigned int capacity_required);
static void min_heap_shift_up(timer_min_heap *heap, unsigned int hole_index, nanoev_timer *timer);
static void min_heap_shift_down(timer_min_heap *heap, unsigned int hole_index, nanoev_timer *timer);

#define NANOEV_TIMER_FLAG_INVOKING_CALLBACK  (0x00000001) /* during callback */
#define NANOEV_TIMER_FLAG_DELETED            (0x80000000) /* mark for delete */

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
    ASSERT(!(timer->flags & NANOEV_TIMER_FLAG_DELETED));

    if (!(timer->flags & NANOEV_TIMER_FLAG_INVOKING_CALLBACK)) {
        timer_min_heap *heap = get_loop_timers(timer->loop);
        min_heap_erase(heap, timer);

        mem_free(timer);
    } else {
        timer->flags |= NANOEV_TIMER_FLAG_DELETED;
    }
}

int nanoev_timer_add(
    nanoev_event *event,
    struct nanoev_timeval after,
    int repeat,
    nanoev_timer_callback callback
    )
{
    nanoev_timer *timer = (nanoev_timer*)event;
    timer_min_heap *heap;

    ASSERT(timer);
    ASSERT(in_loop_thread(timer->loop));
    ASSERT(callback);

    if (timer->min_heap_idx != (unsigned int)-1)
        return NANOEV_ERROR_FAIL;

    timer->after = after;
    timer->repeat = repeat;
    nanoev_loop_now(timer->loop, &timer->timeout);
    time_add(&timer->timeout, &after);
    timer->callback = callback;

    heap = get_loop_timers(timer->loop);
    ASSERT(heap);
    return min_heap_insert(heap, timer);
}

int nanoev_timer_del(
    nanoev_event *event
    )
{
    nanoev_timer *timer = (nanoev_timer*)event;
    timer_min_heap *heap;

    ASSERT(timer);
    ASSERT(in_loop_thread(timer->loop));

    if (timer->min_heap_idx == (unsigned int)-1)
        return NANOEV_ERROR_FAIL;

    heap = get_loop_timers(timer->loop);
    ASSERT(heap);
    min_heap_erase(heap, timer);

    return NANOEV_SUCCESS;
}

/*----------------------------------------------------------------------------*/

void timers_init(timer_min_heap *heap)
{
    ASSERT(heap);
    heap->events = NULL;
    heap->capacity = 0;
    heap->size = 0;
}

void timers_term(timer_min_heap *heap)
{
    ASSERT(heap);
    mem_free(heap->events);
}

unsigned int timers_timeout(timer_min_heap *heap, const struct nanoev_timeval *now)
{
    nanoev_timer *top;
    struct nanoev_timeval tv;

    ASSERT(heap && now);

    if (!heap->size)
        return 0xffffffff;  /* INFINITI */

    top = (nanoev_timer*)heap->events[0];
    tv = top->timeout;
    if (time_cmp(&tv, now) <= 0)
        return 0;

    time_sub(&tv, now);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;  /* convert to milliseconds */
}

void timers_process(timer_min_heap *heap, const struct nanoev_timeval *now)
{
    nanoev_timer *top;

    ASSERT(heap && now);

    while (heap->size) {
        top = (nanoev_timer*)heap->events[0];
        if (time_cmp(&top->timeout, now) > 0)
            break;

        /* Erase from the heap */
        min_heap_erase(heap, top);

        /* Invoke callback */
        top->flags |= NANOEV_TIMER_FLAG_INVOKING_CALLBACK;
        top->callback((nanoev_event*)top);
        top->flags &= ~NANOEV_TIMER_FLAG_INVOKING_CALLBACK;

        if (top->flags & NANOEV_TIMER_FLAG_DELETED) {  /* freed during callback */
            mem_free(top);

        } else if (top->repeat) {  /* Add back if top is a repeating timer */
            top->timeout = *now;

            time_add(&top->timeout, &top->after);

            min_heap_shift_up(heap, heap->size, top);
            heap->size++;
        }
    }
}

void timers_adjust_backward(timer_min_heap *heap, const struct nanoev_timeval *off)
{
    unsigned int i;
    nanoev_timer *timer;

    ASSERT(heap && off);

    for (i = 0; i < heap->size; ++i) {
        timer = (nanoev_timer*)heap->events[i];
        time_sub(&timer->timeout, off);
    }
}

/*----------------------------------------------------------------------------*/

static int __time_greater(nanoev_timer *t0, nanoev_timer *t1)
{
    int ret_code = time_cmp(&t0->timeout, &t1->timeout);
    return ret_code > 0 ? 1 : 0;
}

static int min_heap_insert(timer_min_heap *heap, nanoev_timer *timer)
{
    int ret_code;

    ASSERT(timer->min_heap_idx == (unsigned int)-1);

    ret_code = min_heap_reserve(heap, heap->size + 1);
    if (ret_code != NANOEV_SUCCESS)
        return ret_code;

    min_heap_shift_up(heap, heap->size, timer);
    heap->size++;

    return NANOEV_SUCCESS;
}

static void min_heap_erase(timer_min_heap *heap, nanoev_timer *timer)
{
    nanoev_timer *last;
    unsigned int parent;

    if (timer->min_heap_idx != (unsigned int)-1) {
        /* 容量减1 */
        heap->size--;
        
        /* 假定最后一个节点是在timer所在的位置上（覆盖了timer），然后调整堆 */
        last = (nanoev_timer*)heap->events[heap->size];
        parent = (timer->min_heap_idx - 1) / 2;
        if (timer->min_heap_idx > 0 && __time_greater((nanoev_timer*)heap->events[parent], last))
            min_heap_shift_up(heap, timer->min_heap_idx, last);
        else
            min_heap_shift_down(heap, timer->min_heap_idx, last);

        /* 清空timer在heap中的index */
        timer->min_heap_idx = (unsigned int)-1;
    }
}

static int min_heap_reserve(timer_min_heap *heap, unsigned int capacity_required)
{
    ASSERT(heap);
    ASSERT(capacity_required);

    if (heap->capacity < capacity_required) {
        unsigned int capacity_new;
        nanoev_event** events_new;

        capacity_new = heap->capacity;
        while (capacity_new < capacity_required)
            capacity_new += 64;

        events_new = (nanoev_event**)mem_realloc(
            heap->events, sizeof(nanoev_event*) * capacity_new);
        if (!events_new)
            return NANOEV_ERROR_OUT_OF_MEMORY;

        heap->events = events_new;
        heap->capacity = capacity_new;
    }

    return NANOEV_SUCCESS;
}

static void min_heap_shift_up(timer_min_heap *heap, unsigned int hole_index, nanoev_timer *timer)
{
    /* 循环处理：若parent的timeout比timer的timeout值要大，则交换两者的位置 */
    unsigned int parent = (hole_index - 1) / 2;
    while (hole_index) {
        if (!__time_greater((nanoev_timer*)heap->events[parent], timer)) {
            break;
        }

        heap->events[hole_index] = heap->events[parent];
        ((nanoev_timer*)heap->events[hole_index])->min_heap_idx = hole_index;
        hole_index = parent;

        parent = (hole_index - 1) / 2;
    }

    /* 现在的hole_index就是timer应该在的位置 */
    heap->events[hole_index] = (nanoev_event*)timer;
    timer->min_heap_idx = hole_index;
}

static void min_heap_shift_down(timer_min_heap *heap, unsigned int hole_index, nanoev_timer *timer)
{
    unsigned int min_child = 2 * hole_index + 2;  /* 先假设right_child是较小的那个 */
    while (min_child <= heap->size) {
        if (min_child == heap->size) {
            /* 没有right_child，只有left_child */
            min_child--;
        } else if (__time_greater((nanoev_timer*)heap->events[min_child], 
                    (nanoev_timer*)heap->events[min_child - 1])
                   ) {
            /* left_child的值比right_child小，让min_child指向left_child */
            min_child--;
        }

        if (!__time_greater(timer, (nanoev_timer*)heap->events[min_child])) {
            break;
        }

        /* 将timer和min_child位置互换 */
        heap->events[hole_index] = heap->events[min_child];
        ((nanoev_timer*)heap->events[hole_index])->min_heap_idx = hole_index;
        hole_index = min_child;
        
        min_child = 2 * hole_index + 2;
    }

    /* 现在的hole_index就是timer应该在的位置 */
    heap->events[hole_index] = (nanoev_event*)timer;
    timer->min_heap_idx = hole_index;
}

#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_timer {
    NANOEV_EVENT_FILEDS
    nanoev_timer_node node;
    nanoev_timeval after;
    int repeat;
    nanoev_timer_callback callback;
};
typedef struct nanoev_timer nanoev_timer;

static void timer_event_node_callback(nanoev_timer_node *node);
static int min_heap_insert(timer_min_heap *heap, nanoev_timer_node *node);
static void min_heap_erase(timer_min_heap *heap, nanoev_timer_node *node);
static int min_heap_reserve(timer_min_heap *heap, unsigned int capacity_required);
static void min_heap_shift_up(timer_min_heap *heap, unsigned int hole_index, nanoev_timer_node *node);
static void min_heap_shift_down(timer_min_heap *heap, unsigned int hole_index, nanoev_timer_node *node);

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
    timer_node_init(&timer->node, timer_event_node_callback, timer);

    return (nanoev_event*)timer;
}

void timer_free(nanoev_event *event)
{
    nanoev_timer *timer = (nanoev_timer*)event;
    ASSERT(!(timer->flags & NANOEV_TIMER_FLAG_DELETED));

    if (!(timer->flags & NANOEV_TIMER_FLAG_INVOKING_CALLBACK)) {
        timer_min_heap *heap = get_loop_timers(timer->loop);
        timer_node_del(heap, &timer->node);

        mem_free(timer);
    } else {
        /*
         * The callback may have rearmed the current timer. Remove that heap
         * entry now and prevent repeat reinsertion after the callback returns.
         */
        timer_min_heap *heap = get_loop_timers(timer->loop);
        timer_node_del(heap, &timer->node);
        timer->repeat = 0;
        timer->flags |= NANOEV_TIMER_FLAG_DELETED;
    }
}

int nanoev_timer_add(
    nanoev_event *event,
    nanoev_timeval after,
    int repeat,
    nanoev_timer_callback callback
    )
{
    nanoev_timer *timer = (nanoev_timer*)event;
    timer_min_heap *heap;

    ASSERT(timer);
    ASSERT(in_loop_thread(timer->loop));
    ASSERT(callback);

    if (timer_node_active(&timer->node))
        return NANOEV_ERROR_FAIL;

    if (after.tv_sec < 0 || after.tv_usec < 0 || after.tv_usec >= 1000000)
        return NANOEV_ERROR_INVALID_ARG;

    timer->after = after;
    timer->repeat = repeat;
    nanoev_loop_now(timer->loop, &timer->node.timeout);
    time_add(&timer->node.timeout, &after);
    timer->callback = callback;

    heap = get_loop_timers(timer->loop);
    ASSERT(heap);
    return timer_node_add(heap, &timer->node, &timer->node.timeout);
}

int nanoev_timer_del(
    nanoev_event *event
    )
{
    nanoev_timer *timer = (nanoev_timer*)event;
    timer_min_heap *heap;

    ASSERT(timer);
    ASSERT(in_loop_thread(timer->loop));

    /*
     * timers_process() removes the timer from the heap before invoking its
     * callback. Deleting the current timer from inside the callback therefore
     * only needs to prevent repeat timers from being reinserted.
     */
    if (timer->flags & NANOEV_TIMER_FLAG_INVOKING_CALLBACK) {
        timer->repeat = 0;
        return NANOEV_SUCCESS;
    }

    if (!timer_node_active(&timer->node))
        return NANOEV_ERROR_FAIL;

    heap = get_loop_timers(timer->loop);
    ASSERT(heap);
    timer_node_del(heap, &timer->node);

    return NANOEV_SUCCESS;
}

/*----------------------------------------------------------------------------*/

void timer_node_init(nanoev_timer_node *node, nanoev_timer_node_callback callback, void *userdata)
{
    ASSERT(node);
    ASSERT(callback);

    memset(node, 0, sizeof(*node));
    node->min_heap_idx = (unsigned int)-1;
    node->callback = callback;
    node->userdata = userdata;
}

int timer_node_active(nanoev_timer_node *node)
{
    ASSERT(node);

    return node->min_heap_idx != (unsigned int)-1;
}

int timer_node_add(timer_min_heap *heap, nanoev_timer_node *node, const nanoev_timeval *timeout)
{
    ASSERT(heap);
    ASSERT(node);
    ASSERT(timeout);
    ASSERT(node->callback);

    if (timer_node_active(node)) {
        return NANOEV_ERROR_FAIL;
    }

    node->timeout = *timeout;
    return min_heap_insert(heap, node);
}

void timer_node_del(timer_min_heap *heap, nanoev_timer_node *node)
{
    ASSERT(heap);
    ASSERT(node);

    min_heap_erase(heap, node);
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

void timers_timeout(timer_min_heap *heap, const nanoev_timeval *now, nanoev_timeval *timeout)
{
    nanoev_timer_node *top;

    ASSERT(heap && now);

    if (!heap->size) {
        timeout->tv_sec = -1;
        return;
    }

    top = heap->events[0];
    *timeout = top->timeout;
    if (time_cmp(timeout, now) <= 0) {
        timeout->tv_sec = 0;
        timeout->tv_usec = 0;
    } else {
        time_sub(timeout, now);
    }
}

void timers_process(timer_min_heap *heap, const nanoev_timeval *now)
{
    nanoev_timer_node *top;

    ASSERT(heap && now);

    while (heap->size) {
        top = heap->events[0];
        if (time_cmp(&top->timeout, now) > 0)
            break;

        /* Erase from the heap */
        min_heap_erase(heap, top);

        /* Invoke callback */
        top->callback(top);
    }
}

void timers_adjust_backward(timer_min_heap *heap, const nanoev_timeval *off)
{
    unsigned int i;
    nanoev_timer_node *node;

    ASSERT(heap && off);

    for (i = 0; i < heap->size; ++i) {
        node = heap->events[i];
        time_sub(&node->timeout, off);
    }
}

/*----------------------------------------------------------------------------*/

static void timer_event_node_callback(nanoev_timer_node *node)
{
    nanoev_timer *timer;
    timer_min_heap *heap;

    ASSERT(node);

    timer = (nanoev_timer*)node->userdata;
    ASSERT(timer);

    timer->flags |= NANOEV_TIMER_FLAG_INVOKING_CALLBACK;
    timer->callback((nanoev_event*)timer);
    timer->flags &= ~NANOEV_TIMER_FLAG_INVOKING_CALLBACK;

    if (timer->flags & NANOEV_TIMER_FLAG_DELETED) {
        mem_free(timer);

    } else if (timer->repeat && !timer_node_active(&timer->node)) {
        nanoev_loop_now(timer->loop, &timer->node.timeout);
        time_add(&timer->node.timeout, &timer->after);

        heap = get_loop_timers(timer->loop);
        min_heap_shift_up(heap, heap->size, &timer->node);
        heap->size++;
    }
}

static int __time_greater(nanoev_timer_node *t0, nanoev_timer_node *t1)
{
    int ret_code = time_cmp(&t0->timeout, &t1->timeout);
    return ret_code > 0 ? 1 : 0;
}

static int min_heap_insert(timer_min_heap *heap, nanoev_timer_node *node)
{
    int ret_code;

    ASSERT(node->min_heap_idx == (unsigned int)-1);

    ret_code = min_heap_reserve(heap, heap->size + 1);
    if (ret_code != NANOEV_SUCCESS)
        return ret_code;

    min_heap_shift_up(heap, heap->size, node);
    heap->size++;

    return NANOEV_SUCCESS;
}

static void min_heap_erase(timer_min_heap *heap, nanoev_timer_node *node)
{
    nanoev_timer_node *last;
    unsigned int parent;

    if (node->min_heap_idx != (unsigned int)-1) {
        heap->size--;
        
        last = heap->events[heap->size];
        parent = (node->min_heap_idx - 1) / 2;
        if (node->min_heap_idx > 0 && __time_greater(heap->events[parent], last))
            min_heap_shift_up(heap, node->min_heap_idx, last);
        else
            min_heap_shift_down(heap, node->min_heap_idx, last);

        node->min_heap_idx = (unsigned int)-1;
    }
}

static int min_heap_reserve(timer_min_heap *heap, unsigned int capacity_required)
{
    ASSERT(heap);
    ASSERT(capacity_required);

    if (heap->capacity < capacity_required) {
        unsigned int capacity_new;
        nanoev_timer_node** events_new;

        capacity_new = heap->capacity;
        while (capacity_new < capacity_required)
            capacity_new += 64;

        events_new = (nanoev_timer_node**)mem_realloc(
            heap->events, sizeof(nanoev_timer_node*) * capacity_new);
        if (!events_new)
            return NANOEV_ERROR_OUT_OF_MEMORY;

        heap->events = events_new;
        heap->capacity = capacity_new;
    }

    return NANOEV_SUCCESS;
}

static void min_heap_shift_up(timer_min_heap *heap, unsigned int hole_index, nanoev_timer_node *node)
{
    unsigned int parent = (hole_index - 1) / 2;
    while (hole_index) {
        if (!__time_greater(heap->events[parent], node)) {
            break;
        }

        heap->events[hole_index] = heap->events[parent];
        heap->events[hole_index]->min_heap_idx = hole_index;
        hole_index = parent;

        parent = (hole_index - 1) / 2;
    }

    heap->events[hole_index] = node;
    node->min_heap_idx = hole_index;
}

static void min_heap_shift_down(timer_min_heap *heap, unsigned int hole_index, nanoev_timer_node *node)
{
    unsigned int min_child = 2 * hole_index + 2;
    while (min_child <= heap->size) {
        if (min_child == heap->size) {
            min_child--;
        } else if (__time_greater(heap->events[min_child],
                    heap->events[min_child - 1])
                   ) {
            min_child--;
        }

        if (!__time_greater(node, heap->events[min_child])) {
            break;
        }

        heap->events[hole_index] = heap->events[min_child];
        heap->events[hole_index]->min_heap_idx = hole_index;
        hole_index = min_child;
        
        min_child = 2 * hole_index + 2;
    }

    heap->events[hole_index] = node;
    node->min_heap_idx = hole_index;
}

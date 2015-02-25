#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

struct nanoev_timer {
    NANOEV_EVENT_FILEDS
    struct nanoev_timeval timeout;
    nanoev_timer_callback callback;
    int min_heap_idx;
};
typedef struct nanoev_timer nanoev_timer;

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
    timer->min_heap_idx = -1;

    return (nanoev_event*)timer;
}

void timer_free(nanoev_event *event)
{
    nanoev_timer *timer = (nanoev_timer*)event;

    mem_free(timer);
}

int nanoev_timer_add(
    nanoev_event *event,
    struct nanoev_timeval after,
    nanoev_timer_callback callback
    )
{
    return NANOEV_SUCCESS;
}

int nanoev_timer_del(
    nanoev_event *event
    )
{
    return NANOEV_SUCCESS;
}

/*----------------------------------------------------------------------------*/

void min_heap_free(min_heap *h)
{
    ASSERT(h);
    mem_free(h->events);
}

int min_heap_reserve(min_heap *h, int capacity)
{
    ASSERT(h);
    ASSERT(capacity > 0);

    if (h->capacity < capacity) {
        nanoev_event** events_new = (nanoev_event**)mem_realloc(
            h->events, sizeof(nanoev_event*) * (capacity + 1));
        if (!events_new)
            return NANOEV_ERROR_OUT_OF_MEMORY;
        h->events = events_new;
        h->capacity = capacity;
    }

    return NANOEV_SUCCESS;
}

int min_heap_erase(min_heap *h, nanoev_event *event)
{
    return NANOEV_ERROR_FAIL;
}

nanoev_event* min_heap_top(min_heap *h)
{
    ASSERT(h);
    return (h->size ? h->events[1] : NULL);
}

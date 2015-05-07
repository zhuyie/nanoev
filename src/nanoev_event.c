#include "nanoev_internal.h"

/*----------------------------------------------------------------------------*/

nanoev_event* nanoev_event_new(
    nanoev_event_type type, 
    nanoev_loop *loop, 
    void *userdata
    )
{
    nanoev_event *event;

    ASSERT(loop);
    ASSERT(in_loop_thread(loop));

    switch (type) {
    case nanoev_event_tcp:
        event = tcp_new(loop, userdata);
        break;

    case nanoev_event_udp:
        event = udp_new(loop, userdata);
        break;

    case nanoev_event_async:
        event = async_new(loop, userdata);
        break;

    case nanoev_event_timer:
        event = timer_new(loop, userdata);
        break;

    default:
        ASSERT(0);
        event = NULL;
    }

    return event;
}

void nanoev_event_free(
    nanoev_event *event
    )
{
    ASSERT(event && event->loop);
    ASSERT(in_loop_thread(event->loop));

    switch (event->type) {
    case nanoev_event_tcp:
        tcp_free(event);
        break;

    case nanoev_event_udp:
        udp_free(event);
        break;

    case nanoev_event_async:
        async_free(event);
        break;

    case nanoev_event_timer:
        timer_free(event);
        break;

    default:
        ASSERT(0);
    }
}

nanoev_event_type nanoev_event__type(
    nanoev_event *event
    )
{
    ASSERT(event);
    return event->type;
}

nanoev_loop* nanoev_event_loop(
    nanoev_event *event
    )
{
    ASSERT(event);
    return event->loop;
}

void* nanoev_event_userdata(
    nanoev_event *event
    )
{
    ASSERT(event);
    return event->userdata;
}

/*----------------------------------------------------------------------------*/

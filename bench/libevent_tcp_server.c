#include "tcp.h"
#include "clock.h"
#include "net.h"
#include "protocol.h"
#include "stats.h"

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct event_conn event_conn;

typedef struct event_server {
    const bench_config *config;
    struct event_base *base;
    struct evconnlistener *listener;
    struct event *report_event;
    struct event *signal_event;
    event_conn *head;
    bench_stats stats;
    bench_stats previous;
    bench_timeval started;
    uint64_t previous_us;
} event_server;

struct event_conn {
    event_server *server;
    event_conn *next;
    event_conn *prev;
    struct bufferevent *bev;
};

static void server_signal(evutil_socket_t fd, short events, void *arg);
static void server_report(evutil_socket_t fd, short events, void *arg);
static void server_accept(struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *addr, int len, void *arg);
static void server_accept_error(struct evconnlistener *listener, void *arg);
static void server_read(struct bufferevent *bev, void *arg);
static void server_event(struct bufferevent *bev, short events, void *arg);
static void server_close_connections(event_server *server);
static void server_conn_close(event_conn *conn);
static void server_conn_unlink(event_conn *conn);

int bench_libevent_tcp_server_run(const bench_config *config)
{
    event_server server;
    bench_sockaddr addr;
    struct timeval interval;
    int ret = 1;

    memset(&server, 0, sizeof(server));
    server.config = config;
    bench_stats_init(&server.stats);
    bench_stats_init(&server.previous);

    if (bench_resolve_addr(config, &addr) != 0) {
        fprintf(stderr, "libevent server setup failed: invalid address %s:%u\n",
            config->host, (unsigned int)config->port);
        goto done;
    }
    server.base = event_base_new();
    if (!server.base) {
        fprintf(stderr, "libevent server setup failed: unable to create event base\n");
        goto done;
    }
    server.listener = evconnlistener_new_bind(server.base, server_accept, &server,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, (int)config->backlog,
        (struct sockaddr*)&addr.storage, addr.len);
    if (!server.listener) {
        fprintf(stderr, "libevent server setup failed: listen failed on %s:%u errno=%d\n",
            config->host, (unsigned int)config->port, errno);
        goto done;
    }
    evconnlistener_set_error_cb(server.listener, server_accept_error);

    server.report_event = event_new(server.base, -1, EV_PERSIST, server_report, &server);
    server.signal_event = evsignal_new(server.base, SIGINT, server_signal, &server);
    if (!server.report_event || !server.signal_event) {
        fprintf(stderr, "libevent server setup failed: unable to create control events\n");
        goto done;
    }
    interval.tv_sec = config->report_interval;
    interval.tv_usec = 0;
    if (event_add(server.report_event, &interval) != 0 || event_add(server.signal_event, NULL) != 0) {
        fprintf(stderr, "libevent server setup failed: unable to start control events\n");
        goto done;
    }

    bench_now(&server.started);
    server.previous_us = bench_time_us();
    printf("libevent tcp server listening on %s:%u message_size=%u backlog=%u\n",
        config->host, (unsigned int)config->port, config->message_size, config->backlog);
    printf("press Ctrl+C to stop\n");
    bench_stats_print_delta_header("server", 1);

    if (event_base_dispatch(server.base) != 0)
        fprintf(stderr, "libevent server failed: event loop returned failure\n");

    {
        bench_timeval ended;
        bench_now(&ended);
        bench_stats_print_total("server", &server.stats, bench_time_diff_ms(&server.started, &ended), 1);
    }
    ret = 0;

done:
    server_close_connections(&server);
    if (server.signal_event)
        event_free(server.signal_event);
    if (server.report_event)
        event_free(server.report_event);
    if (server.listener)
        evconnlistener_free(server.listener);
    if (server.base)
        event_base_free(server.base);
    return ret;
}

static void server_signal(evutil_socket_t fd, short events, void *arg)
{
    event_server *server = (event_server*)arg;
    (void)fd;
    (void)events;
    event_base_loopbreak(server->base);
}

static void server_report(evutil_socket_t fd, short events, void *arg)
{
    event_server *server = (event_server*)arg;
    uint64_t now = bench_time_us();
    uint64_t elapsed_ms = (now - server->previous_us) / 1000ULL;
    (void)fd;
    (void)events;

    bench_stats_print_delta("server", &server->stats, &server->previous, elapsed_ms, 1);
    server->previous = server->stats;
    server->previous_us = now;
}

static void server_accept(struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *addr, int len, void *arg)
{
    event_server *server = (event_server*)arg;
    event_conn *conn;
    (void)listener;
    (void)addr;
    (void)len;

    conn = (event_conn*)calloc(1, sizeof(*conn));
    if (!conn) {
        bench_stats_record_accept_error(&server->stats);
        evutil_closesocket(fd);
        return;
    }
    conn->server = server;
    conn->bev = bufferevent_socket_new(server->base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!conn->bev) {
        bench_stats_record_accept_error(&server->stats);
        evutil_closesocket(fd);
        free(conn);
        return;
    }
    conn->next = server->head;
    if (server->head)
        server->head->prev = conn;
    server->head = conn;

    bufferevent_setcb(conn->bev, server_read, NULL, server_event, conn);
    bufferevent_enable(conn->bev, EV_READ | EV_WRITE);
}

static void server_accept_error(struct evconnlistener *listener, void *arg)
{
    event_server *server = (event_server*)arg;
    int error = EVUTIL_SOCKET_ERROR();
    (void)listener;
    bench_stats_record_accept_error(&server->stats);
    fprintf(stderr, "libevent server accept error: %d\n", error);
}

static void server_read(struct bufferevent *bev, void *arg)
{
    event_conn *conn = (event_conn*)arg;
    struct evbuffer *input = bufferevent_get_input(bev);

    while (evbuffer_get_length(input) >= BENCH_FRAME_HEADER_SIZE) {
        unsigned char *data = evbuffer_pullup(input, BENCH_FRAME_HEADER_SIZE);
        unsigned int payload_size;
        unsigned int frame_size;

        if (!data)
            return;
        payload_size = bench_frame_payload_size(data);
        if (payload_size > conn->server->config->message_size) {
            bench_stats_record_io_error(&conn->server->stats);
            server_conn_close(conn);
            return;
        }
        frame_size = BENCH_FRAME_HEADER_SIZE + payload_size;
        if (evbuffer_get_length(input) < frame_size)
            return;
        data = evbuffer_pullup(input, frame_size);
        if (!data)
            return;
        if (bufferevent_write(bev, data, frame_size) != 0) {
            bench_stats_record_io_error(&conn->server->stats);
            server_conn_close(conn);
            return;
        }
        evbuffer_drain(input, frame_size);
        bench_stats_record_request(&conn->server->stats, frame_size);
    }
}

static void server_event(struct bufferevent *bev, short events, void *arg)
{
    event_conn *conn = (event_conn*)arg;
    (void)bev;
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT))
        server_conn_close(conn);
}

static void server_close_connections(event_server *server)
{
    while (server->head)
        server_conn_close(server->head);
}

static void server_conn_unlink(event_conn *conn)
{
    event_server *server = conn->server;

    if (conn->prev)
        conn->prev->next = conn->next;
    else
        server->head = conn->next;
    if (conn->next)
        conn->next->prev = conn->prev;
    conn->prev = NULL;
    conn->next = NULL;
}

static void server_conn_close(event_conn *conn)
{
    server_conn_unlink(conn);
    if (conn->bev)
        bufferevent_free(conn->bev);
    free(conn);
}

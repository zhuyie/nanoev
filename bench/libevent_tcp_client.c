#include "tcp.h"
#include "clock.h"
#include "net.h"
#include "protocol.h"
#include "stats.h"

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct event_client event_client;
typedef struct event_conn event_conn;

struct event_client {
    const bench_config *config;
    struct event_base *base;
    struct event *report_event;
    struct event *stop_event;
    struct event *signal_event;
    event_conn *connections;
    unsigned int active_connections;
    int stopping;
    bench_stats stats;
    bench_stats previous;
    uint64_t previous_us;
    uint64_t deadline_us;
};

struct event_conn {
    event_client *client;
    struct bufferevent *bev;
    unsigned char *frame;
    unsigned int frame_size;
    uint32_t sequence;
    uint64_t request_start_us;
    int closed;
};

static void client_signal(evutil_socket_t fd, short events, void *arg);
static void client_report(evutil_socket_t fd, short events, void *arg);
static void client_stop(evutil_socket_t fd, short events, void *arg);
static int client_send(event_conn *conn);
static void client_read(struct bufferevent *bev, void *arg);
static void client_event(struct bufferevent *bev, short events, void *arg);
static void client_conn_close(event_conn *conn);

int bench_libevent_tcp_client_run(const bench_config *config)
{
    event_client client;
    bench_sockaddr addr;
    struct timeval interval;
    struct timeval duration;
    unsigned int i;
    int ret = 1;

    memset(&client, 0, sizeof(client));
    client.config = config;
    bench_stats_init(&client.stats);
    bench_stats_init(&client.previous);

    if (bench_resolve_addr(config, &addr) != 0) {
        fprintf(stderr, "libevent client setup failed: invalid address %s:%u\n",
            config->host, (unsigned int)config->port);
        goto done;
    }
    client.base = event_base_new();
    if (!client.base) {
        fprintf(stderr, "libevent client setup failed: unable to create event base\n");
        goto done;
    }

    client.connections = (event_conn*)calloc(config->connections, sizeof(*client.connections));
    if (!client.connections) {
        fprintf(stderr, "libevent client setup failed: unable to allocate %u connections\n",
            config->connections);
        goto done;
    }

    for (i = 0; i < config->connections; i++) {
        event_conn *conn = &client.connections[i];

        conn->client = &client;
        conn->frame_size = BENCH_FRAME_HEADER_SIZE + config->message_size;
        conn->frame = (unsigned char*)malloc(conn->frame_size);
        if (!conn->frame) {
            fprintf(stderr, "libevent client setup failed: unable to allocate connection %u frame\n", i);
            goto done;
        }
        conn->bev = bufferevent_socket_new(client.base, -1, BEV_OPT_CLOSE_ON_FREE);
        if (!conn->bev) {
            fprintf(stderr, "libevent client setup failed: unable to allocate bufferevent %u\n", i);
            goto done;
        }
        bufferevent_setcb(conn->bev, client_read, NULL, client_event, conn);
        bufferevent_enable(conn->bev, EV_READ | EV_WRITE);
        if (bufferevent_socket_connect(conn->bev, (struct sockaddr*)&addr.storage, addr.len) != 0) {
            fprintf(stderr, "libevent client setup failed: connect start failed for connection %u errno=%d\n",
                i, errno);
            goto done;
        }
        client.active_connections++;
    }

    client.report_event = event_new(client.base, -1, EV_PERSIST, client_report, &client);
    client.stop_event = event_new(client.base, -1, 0, client_stop, &client);
    client.signal_event = evsignal_new(client.base, SIGINT, client_signal, &client);
    if (!client.report_event || !client.stop_event || !client.signal_event) {
        fprintf(stderr, "libevent client setup failed: unable to create control events\n");
        goto done;
    }
    interval.tv_sec = config->report_interval;
    interval.tv_usec = 0;
    duration.tv_sec = config->duration + 1;
    duration.tv_usec = 0;
    if (event_add(client.report_event, &interval) != 0
        || event_add(client.stop_event, &duration) != 0
        || event_add(client.signal_event, NULL) != 0) {
        fprintf(stderr, "libevent client setup failed: unable to start control events\n");
        goto done;
    }

    client.previous_us = bench_time_us();
    client.deadline_us = client.previous_us + ((uint64_t)config->duration * 1000000ULL);
    printf("libevent tcp client connecting to %s:%u connections=%u duration=%us message_size=%u\n",
        config->host, (unsigned int)config->port, config->connections, config->duration, config->message_size);
    bench_stats_print_delta_header("client", 0);

    event_base_dispatch(client.base);
    bench_stats_print_total("client", &client.stats, (uint64_t)config->duration * 1000ULL, 0);
    ret = 0;

done:
    if (client.connections) {
        for (i = 0; i < config->connections; i++)
            client_conn_close(&client.connections[i]);
        free(client.connections);
    }
    if (client.signal_event)
        event_free(client.signal_event);
    if (client.stop_event)
        event_free(client.stop_event);
    if (client.report_event)
        event_free(client.report_event);
    if (client.base)
        event_base_free(client.base);
    return ret;
}

static void client_signal(evutil_socket_t fd, short events, void *arg)
{
    event_client *client = (event_client*)arg;
    (void)fd;
    (void)events;

    client->stopping = 1;
    client_stop(-1, 0, client);
}

static void client_report(evutil_socket_t fd, short events, void *arg)
{
    event_client *client = (event_client*)arg;
    uint64_t now = bench_time_us();
    uint64_t elapsed_ms = (now - client->previous_us) / 1000ULL;
    (void)fd;
    (void)events;

    bench_stats_print_delta("client", &client->stats, &client->previous, elapsed_ms, 0);
    client->previous = client->stats;
    client->previous_us = now;
}

static void client_stop(evutil_socket_t fd, short events, void *arg)
{
    event_client *client = (event_client*)arg;
    unsigned int i;
    (void)fd;
    (void)events;

    client->stopping = 1;
    for (i = 0; i < client->config->connections; i++)
        client_conn_close(&client->connections[i]);
}

static int client_send(event_conn *conn)
{
    unsigned int i;

    bench_frame_write_header(conn->frame, conn->client->config->message_size, conn->sequence);
    for (i = BENCH_FRAME_HEADER_SIZE; i < conn->frame_size; i++)
        conn->frame[i] = (unsigned char)(conn->sequence + i);
    conn->request_start_us = bench_time_us();
    return bufferevent_write(conn->bev, conn->frame, conn->frame_size);
}

static void client_read(struct bufferevent *bev, void *arg)
{
    event_conn *conn = (event_conn*)arg;
    event_client *client = conn->client;
    struct evbuffer *input = bufferevent_get_input(bev);

    while (evbuffer_get_length(input) >= BENCH_FRAME_HEADER_SIZE) {
        unsigned char *data = evbuffer_pullup(input, BENCH_FRAME_HEADER_SIZE);
        unsigned int payload_size;
        unsigned int frame_size;
        uint32_t sequence;
        uint64_t now;

        if (!data)
            return;
        payload_size = bench_frame_payload_size(data);
        if (payload_size != client->config->message_size) {
            bench_stats_record_error(&client->stats);
            client_conn_close(conn);
            return;
        }
        frame_size = BENCH_FRAME_HEADER_SIZE + payload_size;
        if (evbuffer_get_length(input) < frame_size)
            return;
        data = evbuffer_pullup(input, frame_size);
        if (!data)
            return;
        sequence = bench_frame_sequence(data);
        if (sequence != conn->sequence) {
            bench_stats_record_error(&client->stats);
            client_conn_close(conn);
            return;
        }

        evbuffer_drain(input, frame_size);
        now = bench_time_us();
        bench_stats_record_request(&client->stats, frame_size);
        bench_stats_record_latency(&client->stats, now - conn->request_start_us);
        conn->sequence++;

        if (client->stopping || now >= client->deadline_us) {
            client->stopping = 1;
            client_conn_close(conn);
            return;
        }
        if (client_send(conn) != 0) {
            bench_stats_record_error(&client->stats);
            client_conn_close(conn);
            return;
        }
    }
}

static void client_event(struct bufferevent *bev, short events, void *arg)
{
    event_conn *conn = (event_conn*)arg;
    (void)bev;

    if (events & BEV_EVENT_CONNECTED) {
        if (client_send(conn) != 0) {
            bench_stats_record_error(&conn->client->stats);
            client_conn_close(conn);
        }
        return;
    }
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        if (!conn->client->stopping)
            bench_stats_record_error(&conn->client->stats);
        client_conn_close(conn);
    }
}

static void client_conn_close(event_conn *conn)
{
    if (conn->closed)
        return;
    conn->closed = 1;
    if (conn->bev)
        bufferevent_free(conn->bev);
    conn->bev = NULL;
    free(conn->frame);
    conn->frame = NULL;
    if (conn->client && conn->client->active_connections > 0) {
        conn->client->active_connections--;
        if (!conn->client->active_connections)
            event_base_loopbreak(conn->client->base);
    }
}

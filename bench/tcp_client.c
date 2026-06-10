#include "tcp.h"
#include "protocol.h"
#include "stats.h"
#include "nanoev.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT assert

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#else
# include <signal.h>
#endif

typedef enum tcp_phase {
    tcp_phase_read_header = 0,
    tcp_phase_read_payload,
    tcp_phase_write
} tcp_phase;

typedef struct tcp_client tcp_client;
typedef struct tcp_client_conn tcp_client_conn;

struct tcp_client_conn {
    tcp_client *client;
    nanoev_event *tcp;
    tcp_phase phase;
    unsigned char *buf;
    unsigned int frame_size;
    unsigned int progress;
    uint32_t sequence;
    uint64_t request_start_us;
    int closed;
};

struct tcp_client {
    const bench_config *config;
    nanoev_loop *loop;
    nanoev_event *async;
    nanoev_event *stop_timer;
    nanoev_event *report_timer;
    tcp_client_conn *connections;
    unsigned int active_connections;
    int stopping;
    bench_stats stats;
    bench_stats previous;
    nanoev_timeval started;
    uint64_t previous_us;
    uint64_t deadline_us;
};

static nanoev_event *signal_async;

static void on_signal_async(nanoev_event *async);
static int install_signal_handler(nanoev_event *async);
static void conn_close(tcp_client_conn *conn);
static int conn_send(tcp_client_conn *conn);
static int conn_read_header(tcp_client_conn *conn);
static int conn_read_payload(tcp_client_conn *conn);
static void on_connect(nanoev_event *tcp, int status);
static void on_write(nanoev_event *tcp, int status, void *buf, unsigned int bytes);
static void on_read(nanoev_event *tcp, int status, void *buf, unsigned int bytes);
static void on_stop(nanoev_event *timer);
static void on_report(nanoev_event *timer);

#ifdef _WIN32
static BOOL WINAPI ctrl_handler(DWORD type)
{
    (void)type;
    if (signal_async)
        nanoev_async_send(signal_async);
    return TRUE;
}
#else
static void sigint_handler(int sig)
{
    (void)sig;
    if (signal_async)
        nanoev_async_send(signal_async);
}
#endif

int bench_tcp_client_run(const bench_config *config)
{
    tcp_client client;
    struct nanoev_addr addr;
    nanoev_timeval interval;
    nanoev_timeval duration;
    unsigned int i;
    int ret;

    memset(&client, 0, sizeof(client));
    client.config = config;
    bench_stats_init(&client.stats);
    bench_stats_init(&client.previous);

    ret = nanoev_init();
    if (ret != NANOEV_SUCCESS)
        return 1;

    client.loop = nanoev_loop_new(NULL);
    if (!client.loop)
        goto fail;

    client.async = nanoev_event_new(nanoev_event_async, client.loop, NULL);
    client.stop_timer = nanoev_event_new(nanoev_event_timer, client.loop, &client);
    client.report_timer = nanoev_event_new(nanoev_event_timer, client.loop, &client);
    if (!client.async || !client.stop_timer || !client.report_timer)
        goto fail;
    if (nanoev_async_start(client.async, on_signal_async) != NANOEV_SUCCESS)
        goto fail;
    if (install_signal_handler(client.async))
        goto fail;

    client.connections = (tcp_client_conn*)calloc(config->connections, sizeof(*client.connections));
    if (!client.connections)
        goto fail;
    if (nanoev_addr_init(&addr, config->family, config->host, config->port) != NANOEV_SUCCESS)
        goto fail;

    for (i = 0; i < config->connections; i++) {
        tcp_client_conn *conn = &client.connections[i];

        conn->client = &client;
        conn->frame_size = BENCH_FRAME_HEADER_SIZE + config->message_size;
        conn->buf = (unsigned char*)malloc(conn->frame_size);
        if (!conn->buf)
            goto fail;
        conn->tcp = nanoev_event_new(nanoev_event_tcp, client.loop, conn);
        if (!conn->tcp)
            goto fail;
        if (nanoev_tcp_connect(conn->tcp, &addr, NULL, on_connect) != NANOEV_SUCCESS)
            goto fail;
        client.active_connections++;
    }

    interval.tv_sec = config->report_interval;
    interval.tv_usec = 0;
    duration.tv_sec = config->duration + 1;
    duration.tv_usec = 0;
    if (nanoev_timer_add(client.report_timer, interval, 1, on_report) != NANOEV_SUCCESS)
        goto fail;
    if (nanoev_timer_add(client.stop_timer, duration, 0, on_stop) != NANOEV_SUCCESS)
        goto fail;

    nanoev_now(&client.started);
    client.previous_us = bench_time_us();
    client.deadline_us = client.previous_us + ((uint64_t)config->duration * 1000000ULL);
    printf("tcp client connecting to %s:%u connections=%u duration=%us message_size=%u\n",
        config->host, (unsigned int)config->port, config->connections, config->duration, config->message_size);
    bench_stats_print_delta_header("client");

    ret = nanoev_loop_run(client.loop);
    if (ret != NANOEV_SUCCESS)
        goto fail;

    bench_stats_print_total("client", &client.stats, (uint64_t)config->duration * 1000ULL);

    for (i = 0; i < config->connections; i++)
        conn_close(&client.connections[i]);
    free(client.connections);
    nanoev_event_free(client.report_timer);
    nanoev_event_free(client.stop_timer);
    nanoev_event_free(client.async);
    nanoev_loop_free(client.loop);
    nanoev_term();
    return 0;

fail:
    if (client.connections) {
        for (i = 0; i < config->connections; i++)
            conn_close(&client.connections[i]);
        free(client.connections);
    }
    if (client.report_timer)
        nanoev_event_free(client.report_timer);
    if (client.stop_timer)
        nanoev_event_free(client.stop_timer);
    if (client.async)
        nanoev_event_free(client.async);
    if (client.loop)
        nanoev_loop_free(client.loop);
    nanoev_term();
    return 1;
}

static void on_signal_async(nanoev_event *async)
{
    nanoev_loop_break(nanoev_event_loop(async));
}

static int install_signal_handler(nanoev_event *async)
{
    signal_async = async;
#ifdef _WIN32
    return SetConsoleCtrlHandler(ctrl_handler, TRUE) ? 0 : -1;
#else
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sigint_handler);
    return 0;
#endif
}

static void conn_close(tcp_client_conn *conn)
{
    if (conn->closed)
        return;
    conn->closed = 1;
    if (conn->tcp)
        nanoev_event_free(conn->tcp);
    conn->tcp = NULL;
    free(conn->buf);
    conn->buf = NULL;
    if (conn->client && conn->client->active_connections > 0) {
        conn->client->active_connections--;
        if (!conn->client->active_connections)
            nanoev_loop_break(conn->client->loop);
    }
}

static int conn_send(tcp_client_conn *conn)
{
    unsigned int i;

    bench_frame_write_header(conn->buf, conn->client->config->message_size, conn->sequence);
    for (i = BENCH_FRAME_HEADER_SIZE; i < conn->frame_size; i++)
        conn->buf[i] = (unsigned char)(conn->sequence + i);

    conn->phase = tcp_phase_write;
    conn->progress = 0;
    conn->request_start_us = bench_time_us();
    return nanoev_tcp_write(conn->tcp, conn->buf, conn->frame_size, NULL, on_write) == NANOEV_SUCCESS ? 0 : -1;
}

static int conn_read_header(tcp_client_conn *conn)
{
    conn->phase = tcp_phase_read_header;
    conn->progress = 0;
    return nanoev_tcp_read(conn->tcp, conn->buf, BENCH_FRAME_HEADER_SIZE, NULL, on_read) == NANOEV_SUCCESS ? 0 : -1;
}

static int conn_read_payload(tcp_client_conn *conn)
{
    unsigned int payload_size = bench_frame_payload_size(conn->buf);

    if (payload_size != conn->client->config->message_size)
        return -1;
    conn->phase = tcp_phase_read_payload;
    conn->progress = BENCH_FRAME_HEADER_SIZE;
    if (!payload_size)
        return 0;
    return nanoev_tcp_read(conn->tcp, conn->buf + conn->progress, payload_size, NULL, on_read) == NANOEV_SUCCESS ? 0 : -1;
}

static void on_connect(nanoev_event *tcp, int status)
{
    tcp_client_conn *conn = (tcp_client_conn*)nanoev_event_userdata(tcp);

    if (status) {
        if (!conn->client->stopping)
            bench_stats_record_error(&conn->client->stats);
        conn_close(conn);
        return;
    }

    if (conn_send(conn) != 0) {
        bench_stats_record_error(&conn->client->stats);
        conn_close(conn);
    }
}

static void on_write(nanoev_event *tcp, int status, void *buf, unsigned int bytes)
{
    tcp_client_conn *conn = (tcp_client_conn*)nanoev_event_userdata(tcp);
    int ret;
    (void)buf;

    if (status) {
        if (!conn->client->stopping)
            bench_stats_record_error(&conn->client->stats);
        conn_close(conn);
        return;
    }

    conn->progress += bytes;
    if (conn->progress < conn->frame_size) {
        ret = nanoev_tcp_write(tcp, conn->buf + conn->progress, conn->frame_size - conn->progress, NULL, on_write);
        if (ret != NANOEV_SUCCESS) {
            bench_stats_record_error(&conn->client->stats);
            conn_close(conn);
        }
        return;
    }

    if (conn_read_header(conn) != 0) {
        bench_stats_record_error(&conn->client->stats);
        conn_close(conn);
    }
}

static void on_read(nanoev_event *tcp, int status, void *buf, unsigned int bytes)
{
    tcp_client_conn *conn = (tcp_client_conn*)nanoev_event_userdata(tcp);
    int ret = 0;
    (void)buf;

    if (status || !bytes) {
        if (!conn->client->stopping)
            bench_stats_record_error(&conn->client->stats);
        conn_close(conn);
        return;
    }

    conn->progress += bytes;
    if (conn->phase == tcp_phase_read_header) {
        if (conn->progress < BENCH_FRAME_HEADER_SIZE) {
            ret = nanoev_tcp_read(tcp, conn->buf + conn->progress, BENCH_FRAME_HEADER_SIZE - conn->progress, NULL, on_read);
        } else {
            ret = conn_read_payload(conn);
        }
    } else if (conn->progress < conn->frame_size) {
        ret = nanoev_tcp_read(tcp, conn->buf + conn->progress, conn->frame_size - conn->progress, NULL, on_read);
    } else {
        uint32_t sequence = bench_frame_sequence(conn->buf);

        if (sequence != conn->sequence) {
            bench_stats_record_error(&conn->client->stats);
            conn_close(conn);
            return;
        }

        bench_stats_record_request(&conn->client->stats, conn->frame_size);
        uint64_t now = bench_time_us();

        bench_stats_record_latency(&conn->client->stats, now - conn->request_start_us);
        conn->sequence++;
        if (conn->client->stopping || now >= conn->client->deadline_us) {
            conn->client->stopping = 1;
            conn_close(conn);
            return;
        }
        ret = conn_send(conn);
    }

    if (ret != 0) {
        bench_stats_record_error(&conn->client->stats);
        conn_close(conn);
    }
}

static void on_stop(nanoev_event *timer)
{
    tcp_client *client = (tcp_client*)nanoev_event_userdata(timer);
    unsigned int i;

    client->stopping = 1;
    for (i = 0; i < client->config->connections; i++)
        conn_close(&client->connections[i]);
}

static void on_report(nanoev_event *timer)
{
    tcp_client *client = (tcp_client*)nanoev_event_userdata(timer);
    uint64_t now = bench_time_us();
    uint64_t elapsed_ms = (now - client->previous_us) / 1000ULL;

    bench_stats_print_delta("client", &client->stats, &client->previous, elapsed_ms);
    client->previous = client->stats;
    client->previous_us = now;
}

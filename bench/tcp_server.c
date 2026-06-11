#include "tcp.h"
#include "clock.h"
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

typedef struct tcp_server tcp_server;
typedef struct tcp_server_conn tcp_server_conn;

struct tcp_server_conn {
    tcp_server *server;
    tcp_server_conn *next;
    tcp_server_conn *prev;
    nanoev_event *tcp;
    tcp_phase phase;
    unsigned char *buf;
    unsigned int capacity;
    unsigned int frame_size;
    unsigned int progress;
};

struct tcp_server {
    const bench_config *config;
    nanoev_loop *loop;
    nanoev_event *listener;
    nanoev_event *async;
    nanoev_event *report_timer;
    tcp_server_conn *head;
    bench_stats stats;
    bench_stats previous;
    bench_timeval started;
    uint64_t previous_us;
};

static nanoev_event *signal_async;

static void on_signal_async(nanoev_event *async);
static int install_signal_handler(nanoev_event *async);
static void on_accept(nanoev_event *tcp, int status, nanoev_event *tcp_new);
static int server_accept_next(tcp_server *server, nanoev_event *tcp);
static void server_close_connections(tcp_server *server);
static void* alloc_userdata(void *context, void *userdata);
static void conn_close(tcp_server_conn *conn);
static void conn_unlink(tcp_server_conn *conn);
static int conn_read_header(tcp_server_conn *conn);
static int conn_read_payload(tcp_server_conn *conn);
static int conn_write(tcp_server_conn *conn);
static void on_read(nanoev_event *tcp, int status, void *buf, unsigned int bytes);
static void on_write(nanoev_event *tcp, int status, void *buf, unsigned int bytes);
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

int bench_nanoev_tcp_server_run(const bench_config *config)
{
    tcp_server server;
    struct nanoev_addr addr;
    nanoev_timeval interval;
    int ret;

    memset(&server, 0, sizeof(server));
    server.config = config;
    bench_stats_init(&server.stats);
    bench_stats_init(&server.previous);

    ret = nanoev_init();
    if (ret != NANOEV_SUCCESS) {
        fprintf(stderr, "server setup failed: nanoev_init returned %d\n", ret);
        return 1;
    }

    server.loop = nanoev_loop_new(NULL);
    if (!server.loop) {
        fprintf(stderr, "server setup failed: unable to create loop\n");
        goto fail;
    }

    server.listener = nanoev_event_new(nanoev_event_tcp, server.loop, &server);
    server.async = nanoev_event_new(nanoev_event_async, server.loop, NULL);
    server.report_timer = nanoev_event_new(nanoev_event_timer, server.loop, &server);
    if (!server.listener || !server.async || !server.report_timer) {
        fprintf(stderr, "server setup failed: unable to create control events\n");
        goto fail;
    }

    if (nanoev_addr_init(&addr, config->family == bench_family_ipv6 ? NANOEV_AF_INET6 : NANOEV_AF_INET,
        config->host, config->port) != NANOEV_SUCCESS) {
        fprintf(stderr, "server setup failed: invalid address %s:%u\n",
            config->host, (unsigned int)config->port);
        goto fail;
    }
    if (nanoev_tcp_listen(server.listener, &addr, (int)config->backlog) != NANOEV_SUCCESS) {
        fprintf(stderr, "server setup failed: listen failed on %s:%u, socket_error=%d\n",
            config->host, (unsigned int)config->port, nanoev_tcp_error(server.listener));
        goto fail;
    }
    if (nanoev_tcp_accept(server.listener, NULL, on_accept, alloc_userdata) != NANOEV_SUCCESS) {
        fprintf(stderr, "server setup failed: accept start failed, socket_error=%d\n",
            nanoev_tcp_error(server.listener));
        goto fail;
    }
    if (nanoev_async_start(server.async, on_signal_async) != NANOEV_SUCCESS) {
        fprintf(stderr, "server setup failed: unable to start signal async\n");
        goto fail;
    }
    if (install_signal_handler(server.async)) {
        fprintf(stderr, "server setup failed: unable to install signal handler\n");
        goto fail;
    }

    interval.tv_sec = config->report_interval;
    interval.tv_usec = 0;
    if (nanoev_timer_add(server.report_timer, interval, 1, on_report) != NANOEV_SUCCESS) {
        fprintf(stderr, "server setup failed: unable to start report timer\n");
        goto fail;
    }

    bench_now(&server.started);
    server.previous_us = bench_time_us();
    printf("tcp server listening on %s:%u message_size=%u backlog=%u\n",
        config->host, (unsigned int)config->port, config->message_size, config->backlog);
    printf("press Ctrl+C to stop\n");
    bench_stats_print_delta_header("server", 1);

    ret = nanoev_loop_run(server.loop);
    if (ret != NANOEV_SUCCESS) {
        fprintf(stderr, "server failed: loop returned %d\n", ret);
        goto fail;
    }

    {
        bench_timeval ended;
        bench_now(&ended);
        bench_stats_print_total("server", &server.stats, bench_time_diff_ms(&server.started, &ended), 1);
    }

    server_close_connections(&server);
    nanoev_event_free(server.report_timer);
    nanoev_event_free(server.async);
    nanoev_event_free(server.listener);
    nanoev_loop_free(server.loop);
    nanoev_term();
    return 0;

fail:
    server_close_connections(&server);
    if (server.report_timer)
        nanoev_event_free(server.report_timer);
    if (server.async)
        nanoev_event_free(server.async);
    if (server.listener)
        nanoev_event_free(server.listener);
    if (server.loop)
        nanoev_loop_free(server.loop);
    nanoev_term();
    return 1;
}

static void on_signal_async(nanoev_event *async)
{
    nanoev_loop_break(nanoev_event_loop(async));
}

static void server_close_connections(tcp_server *server)
{
    while (server->head)
        conn_close(server->head);
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

static void* alloc_userdata(void *context, void *userdata)
{
    tcp_server *server = (tcp_server*)context;
    tcp_server_conn *conn;

    if (userdata) {
        conn = (tcp_server_conn*)userdata;
        conn_unlink(conn);
        free(conn->buf);
        free(conn);
        return NULL;
    }

    conn = (tcp_server_conn*)calloc(1, sizeof(*conn));
    if (!conn)
        return NULL;
    conn->server = server;
    conn->capacity = BENCH_FRAME_HEADER_SIZE + server->config->message_size;
    conn->buf = (unsigned char*)malloc(conn->capacity);
    if (!conn->buf) {
        free(conn);
        return NULL;
    }
    conn->next = server->head;
    if (server->head)
        server->head->prev = conn;
    server->head = conn;
    return conn;
}

static void on_accept(nanoev_event *tcp, int status, nanoev_event *tcp_new)
{
    tcp_server *server = (tcp_server*)nanoev_event_userdata(tcp);
    tcp_server_conn *conn;

    if (status || !tcp_new) {
        bench_stats_record_accept_error(&server->stats);
        if (server_accept_next(server, tcp) != 0)
            nanoev_loop_break(server->loop);
        return;
    }

    conn = (tcp_server_conn*)nanoev_event_userdata(tcp_new);
    ASSERT(conn);
    conn->tcp = tcp_new;

    if (conn_read_header(conn) != 0) {
        bench_stats_record_error(&server->stats);
        conn_close(conn);
    }

    if (server_accept_next(server, tcp) != 0)
        nanoev_loop_break(server->loop);
}

static int server_accept_next(tcp_server *server, nanoev_event *tcp)
{
    if (nanoev_tcp_accept(tcp, NULL, on_accept, alloc_userdata) == NANOEV_SUCCESS)
        return 0;
    bench_stats_record_accept_error(&server->stats);
    fprintf(stderr, "server accept failed: accept start failed, socket_error=%d\n", nanoev_tcp_error(tcp));
    return -1;
}

static void conn_unlink(tcp_server_conn *conn)
{
    tcp_server *server = conn->server;

    if (conn->prev)
        conn->prev->next = conn->next;
    else
        server->head = conn->next;
    if (conn->next)
        conn->next->prev = conn->prev;
    conn->prev = NULL;
    conn->next = NULL;
}

static void conn_close(tcp_server_conn *conn)
{
    conn_unlink(conn);
    if (conn->tcp)
        nanoev_event_free(conn->tcp);
    conn->tcp = NULL;
    free(conn->buf);
    conn->buf = NULL;
    free(conn);
}

static int conn_read_header(tcp_server_conn *conn)
{
    conn->phase = tcp_phase_read_header;
    conn->frame_size = BENCH_FRAME_HEADER_SIZE;
    conn->progress = 0;
    return nanoev_tcp_read(conn->tcp, conn->buf, BENCH_FRAME_HEADER_SIZE, NULL, on_read) == NANOEV_SUCCESS ? 0 : -1;
}

static int conn_read_payload(tcp_server_conn *conn)
{
    unsigned int payload_size = bench_frame_payload_size(conn->buf);

    if (payload_size > conn->server->config->message_size)
        return -1;
    conn->phase = tcp_phase_read_payload;
    conn->frame_size = BENCH_FRAME_HEADER_SIZE + payload_size;
    conn->progress = BENCH_FRAME_HEADER_SIZE;
    if (!payload_size)
        return conn_write(conn);
    return nanoev_tcp_read(conn->tcp, conn->buf + conn->progress, payload_size, NULL, on_read) == NANOEV_SUCCESS ? 0 : -1;
}

static int conn_write(tcp_server_conn *conn)
{
    conn->phase = tcp_phase_write;
    conn->progress = 0;
    return nanoev_tcp_write(conn->tcp, conn->buf, conn->frame_size, NULL, on_write) == NANOEV_SUCCESS ? 0 : -1;
}

static void on_read(nanoev_event *tcp, int status, void *buf, unsigned int bytes)
{
    tcp_server_conn *conn = (tcp_server_conn*)nanoev_event_userdata(tcp);
    int ret = 0;
    (void)buf;

    if (status || !bytes) {
        conn_close(conn);
        return;
    }

    conn->progress += bytes;
    if (conn->progress < conn->frame_size) {
        ret = nanoev_tcp_read(tcp, conn->buf + conn->progress, conn->frame_size - conn->progress, NULL, on_read);
        if (ret != NANOEV_SUCCESS) {
            bench_stats_record_error(&conn->server->stats);
            conn_close(conn);
        }
        return;
    }

    if (conn->phase == tcp_phase_read_header)
        ret = conn_read_payload(conn);
    else
        ret = conn_write(conn);

    if (ret != 0) {
        bench_stats_record_error(&conn->server->stats);
        conn_close(conn);
    }
}

static void on_write(nanoev_event *tcp, int status, void *buf, unsigned int bytes)
{
    tcp_server_conn *conn = (tcp_server_conn*)nanoev_event_userdata(tcp);
    int ret;
    (void)buf;

    if (status) {
        conn_close(conn);
        return;
    }

    conn->progress += bytes;
    if (conn->progress < conn->frame_size) {
        ret = nanoev_tcp_write(tcp, conn->buf + conn->progress, conn->frame_size - conn->progress, NULL, on_write);
        if (ret != NANOEV_SUCCESS) {
            bench_stats_record_error(&conn->server->stats);
            conn_close(conn);
        }
        return;
    }

    bench_stats_record_request(&conn->server->stats, conn->frame_size);
    if (conn_read_header(conn) != 0) {
        bench_stats_record_error(&conn->server->stats);
        conn_close(conn);
    }
}

static void on_report(nanoev_event *timer)
{
    tcp_server *server = (tcp_server*)nanoev_event_userdata(timer);
    uint64_t now = bench_time_us();
    uint64_t elapsed_ms = (now - server->previous_us) / 1000ULL;

    bench_stats_print_delta("server", &server->stats, &server->previous, elapsed_ms, 1);
    server->previous = server->stats;
    server->previous_us = now;
}

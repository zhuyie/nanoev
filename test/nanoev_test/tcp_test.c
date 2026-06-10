#include "nanoev.h"
#include "test.h"
#include <string.h>

typedef struct tcp_case {
    nanoev_loop *loop;
    nanoev_event *listener;
    nanoev_event *client;
    nanoev_event *accepted;
    nanoev_event *timer;
    char server_buf[4];
    char client_buf[4];
    int accepted_called;
    int connect_called;
    int server_read_called;
    int server_write_called;
    int client_write_called;
    int client_read_called;
    int client_nodelay_result;
    int client_keepalive_result;
    int client_shutdown_result;
    int timed_out;
    int callback_failures;
} tcp_case;

static nanoev_timeval seconds(long sec)
{
    nanoev_timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = 0;
    return tv;
}

static void tcp_note_failure(tcp_case *tc)
{
    tc->callback_failures++;
    nanoev_loop_break(tc->loop);
}

static void on_tcp_timeout(nanoev_event *timer)
{
    tcp_case *tc = (tcp_case*)nanoev_event_userdata(timer);
    tc->timed_out = 1;
    nanoev_loop_break(tc->loop);
}

static void on_server_write(
    nanoev_event *tcp,
    int status,
    void *buf,
    unsigned int bytes
    )
{
    tcp_case *tc = (tcp_case*)nanoev_event_userdata(tcp);
    (void)buf;

    tc->server_write_called++;
    if (status != 0 || bytes != 4) {
        tcp_note_failure(tc);
    }
}

static void on_client_read(
    nanoev_event *tcp,
    int status,
    void *buf,
    unsigned int bytes
    )
{
    tcp_case *tc = (tcp_case*)nanoev_event_userdata(tcp);

    tc->client_read_called++;
    if (status != 0 || bytes != 4 || memcmp(buf, "pong", 4) != 0) {
        tcp_note_failure(tc);
        return;
    }
    tc->client_shutdown_result = nanoev_tcp_shutdown(tcp, NANOEV_TCP_SHUT_WRITE);
    nanoev_loop_break(tc->loop);
}

static void on_server_read(
    nanoev_event *tcp,
    int status,
    void *buf,
    unsigned int bytes
    )
{
    tcp_case *tc = (tcp_case*)nanoev_event_userdata(tcp);
    static const char reply[] = "pong";

    tc->server_read_called++;
    if (status != 0 || bytes != 4 || memcmp(buf, "ping", 4) != 0) {
        tcp_note_failure(tc);
        return;
    }
    if (nanoev_tcp_write(tcp, reply, 4, on_server_write) != NANOEV_SUCCESS) {
        tcp_note_failure(tc);
    }
}

static void on_client_write(
    nanoev_event *tcp,
    int status,
    void *buf,
    unsigned int bytes
    )
{
    tcp_case *tc = (tcp_case*)nanoev_event_userdata(tcp);
    (void)buf;

    tc->client_write_called++;
    if (status != 0 || bytes != 4) {
        tcp_note_failure(tc);
        return;
    }
    if (nanoev_tcp_read(tcp, tc->client_buf, sizeof(tc->client_buf), on_client_read) != NANOEV_SUCCESS) {
        tcp_note_failure(tc);
    }
}

static void on_connect(
    nanoev_event *tcp,
    int status
    )
{
    tcp_case *tc = (tcp_case*)nanoev_event_userdata(tcp);
    static const char request[] = "ping";

    tc->connect_called++;
    if (status != 0) {
        tcp_note_failure(tc);
        return;
    }
    tc->client_nodelay_result = nanoev_tcp_set_nodelay(tcp, 1);
    tc->client_keepalive_result = nanoev_tcp_set_keepalive(tcp, 1);
    if (tc->client_nodelay_result != NANOEV_SUCCESS
        || tc->client_keepalive_result != NANOEV_SUCCESS) {
        tcp_note_failure(tc);
        return;
    }
    if (nanoev_tcp_write(tcp, request, 4, on_client_write) != NANOEV_SUCCESS) {
        tcp_note_failure(tc);
    }
}

static void on_accept(
    nanoev_event *tcp,
    int status,
    nanoev_event *tcp_new
    )
{
    tcp_case *tc = (tcp_case*)nanoev_event_userdata(tcp);

    tc->accepted_called++;
    if (status != 0 || !tcp_new) {
        tcp_note_failure(tc);
        return;
    }

    tc->accepted = tcp_new;
    nanoev_event_set_userdata(tcp_new, tc);
    if (nanoev_tcp_read(tcp_new, tc->server_buf, sizeof(tc->server_buf), on_server_read) != NANOEV_SUCCESS) {
        tcp_note_failure(tc);
    }
}

static void test_tcp_loopback_round_trip(nanoev_test *test)
{
    tcp_case tc;
    struct nanoev_addr addr;
    unsigned short port;
    int ret;

    memset(&tc, 0, sizeof(tc));

    TEST_REQUIRE(test, nanoev_init() == NANOEV_SUCCESS);
    tc.loop = nanoev_loop_new(NULL);
    TEST_REQUIRE(test, tc.loop);

    tc.client = nanoev_event_new(nanoev_event_tcp, tc.loop, &tc);
    TEST_REQUIRE(test, tc.client);
    tc.timer = nanoev_event_new(nanoev_event_timer, tc.loop, &tc);
    TEST_REQUIRE(test, tc.timer);
    tc.client_nodelay_result = NANOEV_ERROR_FAIL;
    tc.client_keepalive_result = NANOEV_ERROR_FAIL;
    tc.client_shutdown_result = NANOEV_ERROR_FAIL;
    TEST_EXPECT(test, nanoev_tcp_set_nodelay(tc.client, 1) == NANOEV_ERROR_ACCESS_DENIED);
    TEST_EXPECT(test, nanoev_tcp_set_keepalive(tc.client, 1) == NANOEV_ERROR_ACCESS_DENIED);
    TEST_EXPECT(test, nanoev_tcp_shutdown(tc.client, NANOEV_TCP_SHUT_WRITE) == NANOEV_ERROR_ACCESS_DENIED);
    TEST_EXPECT(test, nanoev_tcp_shutdown(tc.client, -1) == NANOEV_ERROR_INVALID_ARG);

    tc.listener = nanoev_event_new(nanoev_event_tcp, tc.loop, &tc);
    TEST_REQUIRE(test, tc.listener);
    TEST_EXPECT(test, nanoev_tcp_addr(tc.listener, 1, &addr) == NANOEV_ERROR_ACCESS_DENIED);
    TEST_EXPECT(test, nanoev_tcp_addr(tc.listener, 1, NULL) == NANOEV_ERROR_INVALID_ARG);
    TEST_EXPECT(test, nanoev_addr_init(&addr, NANOEV_AF_INET, "127.0.0.1", 0) == NANOEV_SUCCESS);
    ret = nanoev_tcp_listen(tc.listener, &addr, 1);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }
    TEST_EXPECT(test, nanoev_tcp_addr(tc.listener, 0, &addr) == NANOEV_ERROR_ACCESS_DENIED);
    ret = nanoev_tcp_addr(tc.listener, 1, &addr);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }
    TEST_EXPECT(test, nanoev_addr_get_port(&addr, &port) == NANOEV_SUCCESS);
    TEST_EXPECT(test, port != 0);

    ret = nanoev_tcp_accept(tc.listener, on_accept, NULL);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }
    ret = nanoev_tcp_connect(tc.client, &addr, on_connect);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }
    ret = nanoev_timer_add(tc.timer, seconds(2), 0, on_tcp_timeout);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }
    TEST_EXPECT(test, nanoev_loop_run(tc.loop) == NANOEV_SUCCESS);

    TEST_EXPECT(test, tc.timed_out == 0);
    TEST_EXPECT(test, tc.callback_failures == 0);
    TEST_EXPECT(test, tc.accepted_called == 1);
    TEST_EXPECT(test, tc.connect_called == 1);
    TEST_EXPECT(test, tc.server_read_called == 1);
    TEST_EXPECT(test, tc.server_write_called == 1);
    TEST_EXPECT(test, tc.client_write_called == 1);
    TEST_EXPECT(test, tc.client_read_called == 1);
    TEST_EXPECT(test, tc.client_nodelay_result == NANOEV_SUCCESS);
    TEST_EXPECT(test, tc.client_keepalive_result == NANOEV_SUCCESS);
    TEST_EXPECT(test, tc.client_shutdown_result == NANOEV_SUCCESS);

cleanup:
    if (tc.accepted) {
        nanoev_event_free(tc.accepted);
    }
    if (tc.timer) {
        nanoev_event_free(tc.timer);
    }
    if (tc.client) {
        nanoev_event_free(tc.client);
    }
    if (tc.listener) {
        nanoev_event_free(tc.listener);
    }
    nanoev_loop_free(tc.loop);
    nanoev_term();
}

void test_tcp(nanoev_test *test)
{
    test_tcp_loopback_round_trip(test);
}

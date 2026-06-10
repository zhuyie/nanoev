#include "nanoev.h"
#include "test.h"
#include <string.h>

typedef struct udp_case {
    nanoev_loop *loop;
    nanoev_event *udp;
    nanoev_event *timer;
    char read_buf[4];
    int read_called;
    int write_called;
    int timed_out;
    int callback_failures;
} udp_case;

static nanoev_timeval seconds(long sec)
{
    nanoev_timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = 0;
    return tv;
}

static void udp_note_failure(udp_case *tc)
{
    tc->callback_failures++;
    nanoev_loop_break(tc->loop);
}

static void on_udp_timeout(nanoev_event *timer)
{
    udp_case *tc = (udp_case*)nanoev_event_userdata(timer);
    tc->timed_out = 1;
    nanoev_loop_break(tc->loop);
}

static void on_udp_read(
    nanoev_event *udp,
    int status,
    void *buf,
    unsigned int bytes,
    const struct nanoev_addr *from_addr
    )
{
    udp_case *tc = (udp_case*)nanoev_event_userdata(udp);
    unsigned short port;

    tc->read_called++;
    if (status != 0 || bytes != 4 || memcmp(buf, "ping", 4) != 0 || !from_addr) {
        udp_note_failure(tc);
        return;
    }
    if (nanoev_addr_get_port(from_addr, &port) != NANOEV_SUCCESS || port == 0) {
        udp_note_failure(tc);
        return;
    }
    nanoev_loop_break(tc->loop);
}

static void on_udp_write(
    nanoev_event *udp,
    int status,
    void *buf,
    unsigned int bytes
    )
{
    udp_case *tc = (udp_case*)nanoev_event_userdata(udp);
    (void)buf;

    tc->write_called++;
    if (status != 0 || bytes != 4) {
        udp_note_failure(tc);
    }
}

static void test_udp_loopback_round_trip(nanoev_test *test)
{
    udp_case tc;
    struct nanoev_addr addr;
    unsigned short port;
    int ret;
    static const char request[] = "ping";

    memset(&tc, 0, sizeof(tc));

    TEST_REQUIRE(test, nanoev_init() == NANOEV_SUCCESS);
    tc.loop = nanoev_loop_new(NULL);
    TEST_REQUIRE(test, tc.loop);

    tc.timer = nanoev_event_new(nanoev_event_timer, tc.loop, &tc);
    TEST_REQUIRE(test, tc.timer);
    tc.udp = nanoev_event_new(nanoev_event_udp, tc.loop, &tc);
    TEST_REQUIRE(test, tc.udp);

    TEST_EXPECT(test, nanoev_udp_addr(tc.udp, &addr) == NANOEV_ERROR_ACCESS_DENIED);
    TEST_EXPECT(test, nanoev_udp_addr(tc.udp, NULL) == NANOEV_ERROR_INVALID_ARG);
    TEST_EXPECT(test, nanoev_udp_set_broadcast(tc.udp, 1) == NANOEV_ERROR_ACCESS_DENIED);
    TEST_EXPECT(test, nanoev_addr_init(&addr, NANOEV_AF_INET, "127.0.0.1", 0) == NANOEV_SUCCESS);
    ret = nanoev_udp_bind(tc.udp, &addr);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }
    TEST_EXPECT(test, nanoev_udp_set_broadcast(tc.udp, 1) == NANOEV_SUCCESS);
    ret = nanoev_udp_addr(tc.udp, &addr);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }
    TEST_EXPECT(test, nanoev_addr_get_port(&addr, &port) == NANOEV_SUCCESS);
    TEST_EXPECT(test, port != 0);
    TEST_EXPECT(test, nanoev_udp_write(tc.udp, request, 4, NULL, on_udp_write) == NANOEV_ERROR_INVALID_ARG);
    ret = nanoev_udp_connect(tc.udp, &addr);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }

    ret = nanoev_udp_read(tc.udp, tc.read_buf, sizeof(tc.read_buf), on_udp_read);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }
    ret = nanoev_udp_write(tc.udp, request, 4, NULL, on_udp_write);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }
    ret = nanoev_timer_add(tc.timer, seconds(2), 0, on_udp_timeout);
    TEST_EXPECT(test, ret == NANOEV_SUCCESS);
    if (ret != NANOEV_SUCCESS) {
        goto cleanup;
    }
    TEST_EXPECT(test, nanoev_loop_run(tc.loop) == NANOEV_SUCCESS);

    TEST_EXPECT(test, tc.timed_out == 0);
    TEST_EXPECT(test, tc.callback_failures == 0);
    TEST_EXPECT(test, tc.read_called == 1);
    TEST_EXPECT(test, tc.write_called == 1);

cleanup:
    if (tc.timer) {
        nanoev_event_free(tc.timer);
    }
    if (tc.udp) {
        nanoev_event_free(tc.udp);
    }
    nanoev_loop_free(tc.loop);
    nanoev_term();
}

void test_udp(nanoev_test *test)
{
    test_udp_loopback_round_trip(test);
}

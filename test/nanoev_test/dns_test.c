#include "nanoev.h"
#include "test.h"

typedef struct dns_case {
    nanoev_loop *loop;
    int called;
    int status;
    unsigned int addr_count;
    unsigned short port;
} dns_case;

typedef struct dns_pool_case {
    nanoev_loop *loop;
    int called;
    int failures;
} dns_pool_case;

static nanoev_timeval seconds(long sec)
{
    nanoev_timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = 0;
    return tv;
}

static void on_dns_timeout(nanoev_event *timer)
{
    dns_case *tc = (dns_case*)nanoev_event_userdata(timer);
    nanoev_loop_break(tc->loop);
}

static void on_dns_resolve(
    nanoev_event *dns,
    int status,
    const struct nanoev_addr *addrs,
    unsigned int addr_count
    )
{
    dns_case *tc = (dns_case*)nanoev_event_userdata(dns);

    tc->called = 1;
    tc->status = status;
    tc->addr_count = addr_count;
    if (status == 0 && addr_count > 0) {
        nanoev_addr_get_port(&addrs[0], &tc->port);
    }
    nanoev_loop_break(tc->loop);
}

static void test_dns_resolves_localhost(nanoev_test *test)
{
    dns_case tc;
    nanoev_event *dns;
    nanoev_event *timer;

    TEST_REQUIRE(test, nanoev_init() == NANOEV_SUCCESS);

    tc.loop = nanoev_loop_new(NULL);
    TEST_REQUIRE(test, tc.loop);
    tc.called = 0;
    tc.status = -1;
    tc.addr_count = 0;
    tc.port = 0;

    dns = nanoev_event_new(nanoev_event_dns, tc.loop, &tc);
    TEST_REQUIRE(test, dns);
    timer = nanoev_event_new(nanoev_event_timer, tc.loop, &tc);
    TEST_REQUIRE(test, timer);

    TEST_EXPECT(test, nanoev_timer_add(timer, seconds(2), 0, on_dns_timeout) == NANOEV_SUCCESS);
    TEST_EXPECT(test, nanoev_dns_resolve(dns, "localhost", NANOEV_AF_UNSPEC, 4321, on_dns_resolve) == NANOEV_SUCCESS);
    TEST_EXPECT(test, nanoev_dns_resolve(dns, "localhost", NANOEV_AF_UNSPEC, 4321, on_dns_resolve) == NANOEV_ERROR_ACCESS_DENIED);
    TEST_EXPECT(test, nanoev_loop_run(tc.loop) == NANOEV_SUCCESS);
    TEST_EXPECT(test, tc.called == 1);
    TEST_EXPECT(test, tc.status == 0);
    TEST_EXPECT(test, tc.addr_count > 0);
    TEST_EXPECT(test, tc.port == 4321);

    nanoev_event_free(timer);
    nanoev_event_free(dns);
    nanoev_loop_free(tc.loop);
    nanoev_term();
}

static void on_dns_pool_resolve(
    nanoev_event *dns,
    int status,
    const struct nanoev_addr *addrs,
    unsigned int addr_count
    )
{
    dns_pool_case *tc = (dns_pool_case*)nanoev_event_userdata(dns);

    if (status != 0 || !addrs || addr_count == 0) {
        tc->failures++;
    }
    tc->called++;
    if (tc->called == 6) {
        nanoev_loop_break(tc->loop);
    }
}

static void test_dns_pool_handles_multiple_requests(nanoev_test *test)
{
    dns_pool_case tc;
    nanoev_event *dns[6];
    nanoev_event *timer;
    int i;

    TEST_REQUIRE(test, nanoev_init() == NANOEV_SUCCESS);

    tc.loop = nanoev_loop_new(NULL);
    TEST_REQUIRE(test, tc.loop);
    tc.called = 0;
    tc.failures = 0;

    timer = nanoev_event_new(nanoev_event_timer, tc.loop, &tc);
    TEST_REQUIRE(test, timer);
    TEST_EXPECT(test, nanoev_timer_add(timer, seconds(2), 0, on_dns_timeout) == NANOEV_SUCCESS);

    for (i = 0; i < 6; i++) {
        dns[i] = nanoev_event_new(nanoev_event_dns, tc.loop, &tc);
        TEST_REQUIRE(test, dns[i]);
        TEST_EXPECT(test, nanoev_dns_resolve(dns[i], "localhost", NANOEV_AF_UNSPEC, 80, on_dns_pool_resolve) == NANOEV_SUCCESS);
    }

    TEST_EXPECT(test, nanoev_loop_run(tc.loop) == NANOEV_SUCCESS);
    TEST_EXPECT(test, tc.called == 6);
    TEST_EXPECT(test, tc.failures == 0);

    nanoev_event_free(timer);
    for (i = 0; i < 6; i++) {
        nanoev_event_free(dns[i]);
    }
    nanoev_loop_free(tc.loop);
    nanoev_term();
}

void test_dns(nanoev_test *test)
{
    test_dns_resolves_localhost(test);
    test_dns_pool_handles_multiple_requests(test);
}

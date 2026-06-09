#include "nanoev.h"
#include "test.h"

typedef struct async_case {
    nanoev_loop *loop;
    int fired;
} async_case;

static void on_async(nanoev_event *async)
{
    async_case *tc = (async_case*)nanoev_event_userdata(async);
    tc->fired++;
    nanoev_loop_break(tc->loop);
}

static void test_async_coalesces_sends(nanoev_test *test)
{
    async_case tc;
    nanoev_event *async;

    TEST_REQUIRE(test, nanoev_init() == NANOEV_SUCCESS);
    tc.loop = nanoev_loop_new(NULL);
    TEST_REQUIRE(test, tc.loop);
    tc.fired = 0;
    async = nanoev_event_new(nanoev_event_async, tc.loop, &tc);
    TEST_REQUIRE(test, async);

    TEST_EXPECT(test, nanoev_async_start(async, on_async) == NANOEV_SUCCESS);
    TEST_EXPECT(test, nanoev_async_start(async, on_async) == NANOEV_ERROR_ACCESS_DENIED);
    TEST_EXPECT(test, nanoev_async_send(async) == NANOEV_SUCCESS);
    TEST_EXPECT(test, nanoev_async_send(async) == NANOEV_SUCCESS);
    TEST_EXPECT(test, nanoev_loop_run(tc.loop) == NANOEV_SUCCESS);
    TEST_EXPECT(test, tc.fired == 1);

    nanoev_event_free(async);
    nanoev_loop_free(tc.loop);
    nanoev_term();
}

void test_async(nanoev_test *test)
{
    test_async_coalesces_sends(test);
}

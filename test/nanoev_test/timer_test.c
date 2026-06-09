#include "nanoev.h"
#include "test.h"

typedef struct timer_case {
    nanoev_loop *loop;
    nanoev_event *timer;
    int fired;
} timer_case;

static nanoev_timeval milliseconds(long ms)
{
    nanoev_timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return tv;
}

static void on_oneshot_timer(nanoev_event *timer)
{
    timer_case *tc = (timer_case*)nanoev_event_userdata(timer);
    tc->fired++;
    nanoev_loop_break(tc->loop);
}

static void on_repeat_timer(nanoev_event *timer)
{
    timer_case *tc = (timer_case*)nanoev_event_userdata(timer);
    tc->fired++;
    if (tc->fired == 3) {
        nanoev_loop_break(tc->loop);
    }
}

static void test_oneshot_timer(nanoev_test *test)
{
    timer_case tc;

    TEST_REQUIRE(test, nanoev_init() == NANOEV_SUCCESS);
    tc.loop = nanoev_loop_new(NULL);
    TEST_REQUIRE(test, tc.loop);
    tc.fired = 0;
    tc.timer = nanoev_event_new(nanoev_event_timer, tc.loop, &tc);
    TEST_REQUIRE(test, tc.timer);

    TEST_EXPECT(test, nanoev_timer_add(tc.timer, milliseconds(1), 0, on_oneshot_timer) == NANOEV_SUCCESS);
    TEST_EXPECT(test, nanoev_timer_add(tc.timer, milliseconds(1), 0, on_oneshot_timer) == NANOEV_ERROR_FAIL);
    TEST_EXPECT(test, nanoev_loop_run(tc.loop) == NANOEV_SUCCESS);
    TEST_EXPECT(test, tc.fired == 1);
    TEST_EXPECT(test, nanoev_timer_del(tc.timer) == NANOEV_ERROR_FAIL);

    nanoev_event_free(tc.timer);
    nanoev_loop_free(tc.loop);
    nanoev_term();
}

static void test_repeat_timer(nanoev_test *test)
{
    timer_case tc;

    TEST_REQUIRE(test, nanoev_init() == NANOEV_SUCCESS);
    tc.loop = nanoev_loop_new(NULL);
    TEST_REQUIRE(test, tc.loop);
    tc.fired = 0;
    tc.timer = nanoev_event_new(nanoev_event_timer, tc.loop, &tc);
    TEST_REQUIRE(test, tc.timer);

    TEST_EXPECT(test, nanoev_timer_add(tc.timer, milliseconds(1), 1, on_repeat_timer) == NANOEV_SUCCESS);
    TEST_EXPECT(test, nanoev_loop_run(tc.loop) == NANOEV_SUCCESS);
    TEST_EXPECT(test, tc.fired == 3);

    nanoev_event_free(tc.timer);
    nanoev_loop_free(tc.loop);
    nanoev_term();
}

void test_timer(nanoev_test *test)
{
    test_oneshot_timer(test);
    test_repeat_timer(test);
}

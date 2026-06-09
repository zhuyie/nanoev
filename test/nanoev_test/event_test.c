#include "nanoev.h"
#include "test.h"

static void test_timer_event_accessors(nanoev_test *test)
{
    int userdata0 = 1;
    int userdata1 = 2;
    nanoev_loop *loop;
    nanoev_event *timer;

    TEST_REQUIRE(test, nanoev_init() == NANOEV_SUCCESS);
    loop = nanoev_loop_new(&userdata0);
    TEST_REQUIRE(test, loop);
    timer = nanoev_event_new(nanoev_event_timer, loop, &userdata0);
    TEST_REQUIRE(test, timer);

    TEST_EXPECT(test, nanoev_loop_userdata(loop) == &userdata0);
    TEST_EXPECT(test, nanoev_event_loop(timer) == loop);
    TEST_EXPECT(test, nanoev_event_typeof(timer) == nanoev_event_timer);
    TEST_EXPECT(test, nanoev_event_userdata(timer) == &userdata0);

    nanoev_event_set_userdata(timer, &userdata1);
    TEST_EXPECT(test, nanoev_event_userdata(timer) == &userdata1);

    nanoev_event_free(timer);
    nanoev_loop_free(loop);
    nanoev_term();
}

void test_event(nanoev_test *test)
{
    test_timer_event_accessors(test);
}

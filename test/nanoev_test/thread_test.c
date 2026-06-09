#include "../../source/nanoev_internal.h"
#include "test.h"

typedef struct thread_case {
    mutex lock;
    cond ready;
    cond proceed;
    int started;
    int can_exit;
    int completed;
} thread_case;

static void thread_worker(void *arg)
{
    thread_case *tc = (thread_case*)arg;

    mutex_lock(&tc->lock);
    tc->started = 1;
    cond_signal(&tc->ready);
    while (!tc->can_exit) {
        cond_wait(&tc->proceed, &tc->lock);
    }
    tc->completed = 1;
    mutex_unlock(&tc->lock);
}

static void test_thread_create_cond_and_join(nanoev_test *test)
{
    thread_case tc;
    thread_handle thread;

    TEST_REQUIRE(test, mutex_init(&tc.lock) == 0);
    TEST_REQUIRE(test, cond_init(&tc.ready) == 0);
    TEST_REQUIRE(test, cond_init(&tc.proceed) == 0);

    tc.started = 0;
    tc.can_exit = 0;
    tc.completed = 0;

    TEST_REQUIRE(test, thread_create(&thread, thread_worker, &tc) == NANOEV_SUCCESS);

    mutex_lock(&tc.lock);
    while (!tc.started) {
        cond_wait(&tc.ready, &tc.lock);
    }
    TEST_EXPECT(test, tc.completed == 0);
    tc.can_exit = 1;
    cond_signal(&tc.proceed);
    mutex_unlock(&tc.lock);

    thread_join(thread);

    mutex_lock(&tc.lock);
    TEST_EXPECT(test, tc.completed == 1);
    mutex_unlock(&tc.lock);

    cond_uninit(&tc.proceed);
    cond_uninit(&tc.ready);
    mutex_uninit(&tc.lock);
}

void test_thread(nanoev_test *test)
{
    test_thread_create_cond_and_join(test);
}

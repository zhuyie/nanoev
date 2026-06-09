#include "nanoev.h"
#include "test.h"
#ifndef _WIN32
# include <fcntl.h>
# include <unistd.h>
#endif

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

#ifndef _WIN32
static void test_async_closes_pipe_fd_zero(nanoev_test *test)
{
    async_case tc;
    nanoev_event *async = NULL;
    int saved_stdin = -1;
    int probe = -1;
    int ret;

    TEST_REQUIRE(test, nanoev_init() == NANOEV_SUCCESS);
    tc.loop = nanoev_loop_new(NULL);
    TEST_REQUIRE(test, tc.loop);
    tc.fired = 0;

    saved_stdin = dup(STDIN_FILENO);
    TEST_REQUIRE(test, saved_stdin >= 0);
    ret = close(STDIN_FILENO);
    TEST_EXPECT(test, ret == 0);
    if (ret != 0) {
        goto cleanup;
    }

    async = nanoev_event_new(nanoev_event_async, tc.loop, &tc);
    TEST_EXPECT(test, async);
    if (async) {
        TEST_EXPECT(test, nanoev_async_start(async, on_async) == NANOEV_SUCCESS);
        nanoev_event_free(async);
        async = NULL;
    }

    probe = open("/dev/null", O_RDONLY);
    TEST_EXPECT(test, probe == STDIN_FILENO);
    if (probe >= 0) {
        close(probe);
    }

    TEST_EXPECT(test, dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO);

cleanup:
    if (async) {
        nanoev_event_free(async);
    }
    if (saved_stdin >= 0) {
        if (fcntl(STDIN_FILENO, F_GETFD) == -1) {
            dup2(saved_stdin, STDIN_FILENO);
        }
        close(saved_stdin);
    }
    nanoev_loop_free(tc.loop);
    nanoev_term();
}
#endif

void test_async(nanoev_test *test)
{
    test_async_coalesces_sends(test);
#ifndef _WIN32
    test_async_closes_pipe_fd_zero(test);
#endif
}

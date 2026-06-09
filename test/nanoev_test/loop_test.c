#include "nanoev.h"
#include "test.h"
#ifndef _WIN32
# include <fcntl.h>
# include <unistd.h>
#endif

#ifndef _WIN32
static void test_loop_allows_poller_fd_zero(nanoev_test *test)
{
    nanoev_loop *loop = NULL;
    int saved_stdin;
    int probe = -1;

    TEST_REQUIRE(test, nanoev_init() == NANOEV_SUCCESS);

    saved_stdin = dup(STDIN_FILENO);
    TEST_REQUIRE(test, saved_stdin >= 0);
    TEST_REQUIRE(test, close(STDIN_FILENO) == 0);

    loop = nanoev_loop_new(NULL);
    TEST_EXPECT(test, loop);
    if (loop) {
        nanoev_loop_free(loop);
    }

    probe = open("/dev/null", O_RDONLY);
    TEST_EXPECT(test, probe == STDIN_FILENO);
    if (probe >= 0) {
        close(probe);
    }

    TEST_EXPECT(test, dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO);
    close(saved_stdin);
    nanoev_term();
}
#endif

void test_loop(nanoev_test *test)
{
#ifndef _WIN32
    test_loop_allows_poller_fd_zero(test);
#endif
}

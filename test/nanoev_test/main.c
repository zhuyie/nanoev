#include "test.h"
#include <stdio.h>

void test_addr(nanoev_test *test);
void test_async(nanoev_test *test);
void test_dns(nanoev_test *test);
void test_event(nanoev_test *test);
void test_loop(nanoev_test *test);
void test_tcp(nanoev_test *test);
void test_udp(nanoev_test *test);
void test_thread(nanoev_test *test);
void test_timer(nanoev_test *test);

void test_fail(
    nanoev_test *test,
    const char *file,
    int line,
    const char *expr
    )
{
    test->failures++;
    fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expr);
}

int main(void)
{
    nanoev_test test = { 0, 0 };

    test_addr(&test);
    test_async(&test);
    test_dns(&test);
    test_event(&test);
    test_loop(&test);
    test_tcp(&test);
    test_udp(&test);
    test_thread(&test);
    test_timer(&test);

    printf("%d checks, %d failures\n", test.checks, test.failures);
    return test.failures == 0 ? 0 : 1;
}

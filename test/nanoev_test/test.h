#ifndef NANOEV_TEST_H
#define NANOEV_TEST_H

typedef struct nanoev_test {
    int checks;
    int failures;
} nanoev_test;

void test_fail(
    nanoev_test *test,
    const char *file,
    int line,
    const char *expr
    );

#define TEST_EXPECT(test, expr)                                      \
    do {                                                             \
        (test)->checks++;                                            \
        if (!(expr)) {                                               \
            test_fail((test), __FILE__, __LINE__, #expr);            \
        }                                                            \
    } while (0)

#define TEST_REQUIRE(test, expr)                                     \
    do {                                                             \
        (test)->checks++;                                            \
        if (!(expr)) {                                               \
            test_fail((test), __FILE__, __LINE__, #expr);            \
            return;                                                  \
        }                                                            \
    } while (0)

#endif /* NANOEV_TEST_H */

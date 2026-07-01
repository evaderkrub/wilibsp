#ifndef TEST_UTIL_H
#define TEST_UTIL_H
#include <stdio.h>
static int g_failures = 0;
#define ASSERT_EQ(actual, expected) do { \
    long _a = (long)(actual), _e = (long)(expected); \
    if (_a != _e) { g_failures++; \
        printf("FAIL %s:%d: %s == 0x%lX, expected 0x%lX\n", __FILE__, __LINE__, #actual, _a, _e); } \
} while (0)
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { g_failures++; \
        printf("FAIL %s:%d: %s is false\n", __FILE__, __LINE__, #expr); } \
} while (0)
#define TEST_RETURN() return g_failures ? 1 : 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Minimal test framework - no external dependencies
 */

int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;
const char *g_current_suite = NULL;

#define ASSERT(expr) do { \
    g_tests_run++; \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_tests_failed++; \
        return; \
    } else { \
        g_tests_passed++; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    g_tests_run++; \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s == %s (got %ld vs %ld)\n", \
                __FILE__, __LINE__, #a, #b, (long)(a), (long)(b)); \
        g_tests_failed++; \
        return; \
    } else { \
        g_tests_passed++; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    g_tests_run++; \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL: %s:%d: \"%s\" != \"%s\"\n", \
                __FILE__, __LINE__, (a), (b)); \
        g_tests_failed++; \
        return; \
    } else { \
        g_tests_passed++; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    g_tests_run++; \
    if ((ptr) == NULL) { \
        fprintf(stderr, "  FAIL: %s:%d: %s is NULL\n", __FILE__, __LINE__, #ptr); \
        g_tests_failed++; \
        return; \
    } else { \
        g_tests_passed++; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    g_tests_run++; \
    if ((ptr) != NULL) { \
        fprintf(stderr, "  FAIL: %s:%d: %s is not NULL\n", __FILE__, __LINE__, #ptr); \
        g_tests_failed++; \
        return; \
    } else { \
        g_tests_passed++; \
    } \
} while(0)

#define ASSERT_TRUE(expr)  ASSERT(expr)
#define ASSERT_FALSE(expr) ASSERT(!(expr))

#define RUN_TEST(fn) do { \
    int before_fail = g_tests_failed; \
    fn(); \
    if (g_tests_failed == before_fail) { \
        printf("    PASS: %s\n", #fn); \
    } \
} while(0)

#define BEGIN_SUITE(name) do { \
    g_current_suite = name; \
    printf("\n  Suite: %s\n", name); \
} while(0)

/* Test suite declarations */
void test_suite_event_bus(void);
void test_suite_alloc_tracker(void);
void test_suite_primitives(void);
void test_suite_timeline(void);
void test_suite_hex_view(void);

int main(void) {
    printf("=== Vizsploits Test Suite ===\n");

    test_suite_primitives();
    test_suite_event_bus();
    test_suite_alloc_tracker();
    test_suite_timeline();
    test_suite_hex_view();

    printf("\n=== Results ===\n");
    printf("  Tests run:    %d\n", g_tests_run);
    printf("  Tests passed: %d\n", g_tests_passed);
    printf("  Tests failed: %d\n", g_tests_failed);
    printf("===============\n");

    return g_tests_failed > 0 ? 1 : 0;
}

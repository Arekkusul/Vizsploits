#include "../src/exploits/api/exploit_api.h"
#include "../src/core/event_bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_tests_run, g_tests_passed, g_tests_failed;

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

#define ASSERT_TRUE(expr)  ASSERT(expr)
#define ASSERT_FALSE(expr) ASSERT(!(expr))
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

#define RUN_TEST(fn) do { \
    int before_fail = g_tests_failed; \
    fn(); \
    if (g_tests_failed == before_fail) { \
        printf("    PASS: %s\n", #fn); \
    } \
} while(0)

static void setup(void) {
    event_bus_cleanup();
    exploit_api_cleanup();
    event_bus_init();
    exploit_api_init();
}

static void teardown(void) {
    exploit_api_cleanup();
    event_bus_cleanup();
}

/* Tests */

static void test_alloc_tracking(void) {
    setup();

    void *ptr = prim_alloc(64, "test alloc");
    ASSERT_NOT_NULL(ptr);
    ASSERT_TRUE(prim_is_allocated(ptr));
    ASSERT_FALSE(prim_was_freed(ptr));
    ASSERT_EQ(prim_get_alloc_size(ptr), 64);

    /* Drain events */
    event_bus_clear();
    prim_free(ptr, "test free");
    event_bus_clear();

    teardown();
}

static void test_free_tracking(void) {
    setup();

    void *ptr = prim_alloc(128, "to free");
    event_bus_clear();

    prim_free(ptr, "freed");
    event_bus_clear();

    ASSERT_FALSE(prim_is_allocated(ptr));
    ASSERT_TRUE(prim_was_freed(ptr));

    teardown();
}

static void test_realloc_tracking(void) {
    setup();

    void *ptr = prim_alloc(32, "original");
    event_bus_clear();

    void *new_ptr = prim_realloc(ptr, 256, "resized");
    event_bus_clear();

    ASSERT_NOT_NULL(new_ptr);
    ASSERT_TRUE(prim_is_allocated(new_ptr));
    ASSERT_EQ(prim_get_alloc_size(new_ptr), 256);
    /* Old pointer marked as freed */
    ASSERT_TRUE(prim_was_freed(ptr));

    prim_free(new_ptr, "cleanup");
    event_bus_clear();

    teardown();
}

static void test_double_free_detection(void) {
    setup();

    void *ptr = prim_alloc(64, "double-free target");
    event_bus_clear();

    prim_free(ptr, "first free");
    event_bus_clear();

    ASSERT_TRUE(prim_was_freed(ptr));

    /* prim_free won't actually double-free (it checks internally),
       but prim_was_freed should still return true */
    ASSERT_TRUE(prim_was_freed(ptr));

    teardown();
}

static void test_untracked_pointer(void) {
    setup();

    void *random_ptr = (void*)0xDEADBEEF;
    ASSERT_FALSE(prim_is_allocated(random_ptr));
    ASSERT_FALSE(prim_was_freed(random_ptr));
    ASSERT_EQ(prim_get_alloc_size(random_ptr), 0);

    teardown();
}

static void test_many_allocations(void) {
    setup();

    /* Test dynamic array growth - allocate more than ALLOC_INITIAL_CAPACITY (256) */
    void *ptrs[300];
    for (int i = 0; i < 300; i++) {
        ptrs[i] = prim_alloc(16, "many allocs");
        ASSERT_NOT_NULL(ptrs[i]);
    }
    event_bus_clear();

    /* Verify all are tracked */
    for (int i = 0; i < 300; i++) {
        ASSERT_TRUE(prim_is_allocated(ptrs[i]));
    }

    /* Free all */
    for (int i = 0; i < 300; i++) {
        prim_free(ptrs[i], "free many");
    }
    event_bus_clear();

    teardown();
}

static void test_exploit_registry(void) {
    setup();

    static exploit_t test_exploit = {
        .meta = {
            .name = "Test Exploit",
            .description = "A test exploit",
            .primitives_used = { PRIM_ALLOC, PRIM_FREE, PRIM_NONE }
        },
        .setup = NULL,
        .run = NULL,
        .cleanup = NULL
    };

    ASSERT_EQ(exploit_register(&test_exploit), 0);
    ASSERT_EQ(exploit_count(), 1);

    exploit_t *found = exploit_get("Test Exploit");
    ASSERT_NOT_NULL(found);
    ASSERT(found == &test_exploit);

    exploit_t *by_idx = exploit_get_by_index(0);
    ASSERT(by_idx == &test_exploit);

    /* Duplicate registration should fail */
    ASSERT_EQ(exploit_register(&test_exploit), -1);

    exploit_unregister("Test Exploit");
    ASSERT_EQ(exploit_count(), 0);

    teardown();
}

void test_suite_alloc_tracker(void) {
    printf("\n  Suite: alloc_tracker\n");
    RUN_TEST(test_alloc_tracking);
    RUN_TEST(test_free_tracking);
    RUN_TEST(test_realloc_tracking);
    RUN_TEST(test_double_free_detection);
    RUN_TEST(test_untracked_pointer);
    RUN_TEST(test_many_allocations);
    RUN_TEST(test_exploit_registry);
}

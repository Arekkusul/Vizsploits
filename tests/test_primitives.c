#include "../src/core/primitives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Import test macros from test_main.c */
extern int g_tests_run, g_tests_passed, g_tests_failed;
extern const char *g_current_suite;

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

#define ASSERT_TRUE(expr)  ASSERT(expr)

#define RUN_TEST(fn) do { \
    int before_fail = g_tests_failed; \
    fn(); \
    if (g_tests_failed == before_fail) { \
        printf("    PASS: %s\n", #fn); \
    } \
} while(0)

/* Tests */

static void test_event_create(void) {
    /* Call once to initialize the start_time_ns baseline */
    get_timestamp_ns();

    primitive_event_t evt = primitive_event_create(PRIM_ALLOC);
    ASSERT_EQ(evt.type, PRIM_ALLOC);
    ASSERT_TRUE(evt.timestamp >= 0); /* First call may be 0 */
    ASSERT_TRUE(evt.pid > 0);
    ASSERT_TRUE(evt.is_exploit_relevant);
}

static void test_event_clone(void) {
    primitive_event_t evt = primitive_event_create(PRIM_WRITE);
    evt.description = "test description";
    evt.address = (void*)0x1234;
    evt.size = 64;

    primitive_event_t *clone = primitive_event_clone(&evt);
    ASSERT_NOT_NULL(clone);
    ASSERT_EQ(clone->type, PRIM_WRITE);
    ASSERT_EQ(clone->size, 64);
    ASSERT_STR_EQ(clone->description, "test description");
    /* Deep copy - different pointer */
    ASSERT(clone->description != evt.description);

    primitive_event_free(clone);
}

static void test_event_clone_call(void) {
    primitive_event_t evt = primitive_event_create(PRIM_CALL);
    evt.description = "call event";
    evt.data.call.func_name = "my_function";

    primitive_event_t *clone = primitive_event_clone(&evt);
    ASSERT_NOT_NULL(clone);
    ASSERT_STR_EQ(clone->data.call.func_name, "my_function");
    ASSERT(clone->data.call.func_name != evt.data.call.func_name);

    primitive_event_free(clone);
}

static void test_event_clone_checkpoint(void) {
    primitive_event_t evt = primitive_event_create(PRIM_CHECKPOINT);
    evt.description = "checkpoint msg";
    evt.data.marker.message = "pause here";
    evt.data.marker.checkpoint_id = 42;

    primitive_event_t *clone = primitive_event_clone(&evt);
    ASSERT_NOT_NULL(clone);
    ASSERT_STR_EQ(clone->data.marker.message, "pause here");
    ASSERT_EQ(clone->data.marker.checkpoint_id, 42);
    ASSERT(clone->data.marker.message != evt.data.marker.message);

    primitive_event_free(clone);
}

static void test_event_free_null(void) {
    /* Should not crash */
    primitive_event_free(NULL);
    ASSERT_TRUE(true);
}

static void test_event_clone_null(void) {
    primitive_event_t *clone = primitive_event_clone(NULL);
    ASSERT(clone == NULL);
}

static void test_type_to_string_all(void) {
    /* Verify all types have non-NULL string representations */
    primitive_type_t types[] = {
        PRIM_ALLOC, PRIM_FREE, PRIM_REALLOC,
        PRIM_READ, PRIM_WRITE, PRIM_EXECUTE,
        PRIM_CALL, PRIM_RETURN, PRIM_JUMP, PRIM_BRANCH,
        PRIM_OVERFLOW, PRIM_UAF, PRIM_DOUBLE_FREE,
        PRIM_RACE, PRIM_INTEGER_OVERFLOW, PRIM_FORMAT_STRING,
        PRIM_SYSCALL, PRIM_MMAP, PRIM_MPROTECT,
        PRIM_PRIV_ESCALATION,
        PRIM_CHECKPOINT, PRIM_ANNOTATION,
        PRIM_CUSTOM, PRIM_NONE
    };
    size_t count = sizeof(types) / sizeof(types[0]);

    for (size_t i = 0; i < count; i++) {
        const char *str = primitive_type_to_string(types[i]);
        ASSERT_NOT_NULL(str);
        ASSERT_TRUE(strlen(str) > 0);
    }
}

static void test_type_short_all(void) {
    primitive_type_t types[] = {
        PRIM_ALLOC, PRIM_FREE, PRIM_REALLOC,
        PRIM_READ, PRIM_WRITE, PRIM_EXECUTE,
        PRIM_CALL, PRIM_RETURN, PRIM_JUMP, PRIM_BRANCH,
        PRIM_OVERFLOW, PRIM_UAF, PRIM_DOUBLE_FREE,
        PRIM_RACE, PRIM_INTEGER_OVERFLOW, PRIM_FORMAT_STRING,
        PRIM_SYSCALL, PRIM_MMAP, PRIM_MPROTECT,
        PRIM_PRIV_ESCALATION,
        PRIM_CHECKPOINT, PRIM_ANNOTATION,
        PRIM_CUSTOM, PRIM_NONE
    };
    size_t count = sizeof(types) / sizeof(types[0]);

    for (size_t i = 0; i < count; i++) {
        const char *str = primitive_type_short(types[i]);
        ASSERT_NOT_NULL(str);
        ASSERT_TRUE(strlen(str) > 0);
        ASSERT_TRUE(strlen(str) <= 6); /* Short names are max 6 chars */
    }
}

static void test_type_specific_strings(void) {
    ASSERT_STR_EQ(primitive_type_to_string(PRIM_ALLOC), "ALLOCATE");
    ASSERT_STR_EQ(primitive_type_to_string(PRIM_FREE), "FREE");
    ASSERT_STR_EQ(primitive_type_to_string(PRIM_UAF), "USE-AFTER-FREE");
    ASSERT_STR_EQ(primitive_type_short(PRIM_ALLOC), "ALLOC");
    ASSERT_STR_EQ(primitive_type_short(PRIM_UAF), "UAF");
    ASSERT_STR_EQ(primitive_type_short(PRIM_CHECKPOINT), "CHKPT");
}

static void test_timestamp(void) {
    uint64_t t1 = get_timestamp_ns();
    uint64_t t2 = get_timestamp_ns();
    ASSERT_TRUE(t2 >= t1);
}

void test_suite_primitives(void) {
    printf("\n  Suite: primitives\n");
    RUN_TEST(test_event_create);
    RUN_TEST(test_event_clone);
    RUN_TEST(test_event_clone_call);
    RUN_TEST(test_event_clone_checkpoint);
    RUN_TEST(test_event_free_null);
    RUN_TEST(test_event_clone_null);
    RUN_TEST(test_type_to_string_all);
    RUN_TEST(test_type_short_all);
    RUN_TEST(test_type_specific_strings);
    RUN_TEST(test_timestamp);
}

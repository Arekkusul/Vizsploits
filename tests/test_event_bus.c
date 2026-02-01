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

#define RUN_TEST(fn) do { \
    int before_fail = g_tests_failed; \
    fn(); \
    if (g_tests_failed == before_fail) { \
        printf("    PASS: %s\n", #fn); \
    } \
} while(0)

/* Helper: track received events */
static int g_handler_count = 0;
static primitive_type_t g_last_type = PRIM_NONE;
static int g_last_step = 0;

static void test_handler(const primitive_event_t *evt, void *userdata) {
    (void)userdata;
    g_handler_count++;
    g_last_type = evt->type;
    g_last_step = evt->step_number;
}

static int g_typed_handler_count = 0;
static void typed_handler(const primitive_event_t *evt, void *userdata) {
    (void)userdata;
    (void)evt;
    g_typed_handler_count++;
}

static void reset_handler_state(void) {
    g_handler_count = 0;
    g_last_type = PRIM_NONE;
    g_last_step = 0;
    g_typed_handler_count = 0;
}

/* Tests */

static void test_init_cleanup(void) {
    /* event_bus_init should succeed */
    event_bus_cleanup(); /* clean any prior state */
    ASSERT_EQ(event_bus_init(), 0);
    /* Double init should succeed (idempotent) */
    ASSERT_EQ(event_bus_init(), 0);
    event_bus_cleanup();
}

static void test_emit_process(void) {
    event_bus_cleanup();
    event_bus_init();
    reset_handler_state();

    int sub_id = event_bus_subscribe(PRIM_NONE, test_handler, NULL);
    ASSERT_TRUE(sub_id > 0);

    primitive_event_t evt = primitive_event_create(PRIM_ALLOC);
    evt.description = "test alloc";
    ASSERT_EQ(event_bus_emit(&evt), 0);

    ASSERT_EQ(event_bus_pending_count(), 1);
    int processed = event_bus_process();
    ASSERT_TRUE(processed >= 1);
    ASSERT_EQ(g_handler_count, 1);
    ASSERT_EQ(g_last_type, PRIM_ALLOC);

    event_bus_cleanup();
}

static void test_subscribe_unsubscribe(void) {
    event_bus_cleanup();
    event_bus_init();
    reset_handler_state();

    int sub_id = event_bus_subscribe(PRIM_NONE, test_handler, NULL);
    ASSERT_TRUE(sub_id > 0);

    /* Emit and process */
    primitive_event_t evt = primitive_event_create(PRIM_FREE);
    evt.description = "test free";
    event_bus_emit(&evt);
    event_bus_process();
    ASSERT_EQ(g_handler_count, 1);

    /* Unsubscribe */
    event_bus_unsubscribe(sub_id);
    reset_handler_state();

    event_bus_emit(&evt);
    event_bus_process();
    ASSERT_EQ(g_handler_count, 0); /* Should not receive */

    event_bus_cleanup();
}

static void test_type_filtering(void) {
    event_bus_cleanup();
    event_bus_init();
    reset_handler_state();

    /* Subscribe only to PRIM_ALLOC */
    event_bus_subscribe(PRIM_ALLOC, typed_handler, NULL);

    /* Emit ALLOC */
    primitive_event_t evt1 = primitive_event_create(PRIM_ALLOC);
    evt1.description = "alloc";
    event_bus_emit(&evt1);

    /* Emit FREE (should not be received) */
    primitive_event_t evt2 = primitive_event_create(PRIM_FREE);
    evt2.description = "free";
    event_bus_emit(&evt2);

    event_bus_process();
    ASSERT_EQ(g_typed_handler_count, 1); /* Only ALLOC */

    event_bus_cleanup();
}

static void test_queue_full(void) {
    event_bus_cleanup();
    event_bus_init();

    /* Fill the queue */
    for (int i = 0; i < EVENT_QUEUE_SIZE; i++) {
        primitive_event_t evt = primitive_event_create(PRIM_ALLOC);
        evt.description = "fill";
        int rc = event_bus_emit(&evt);
        ASSERT_EQ(rc, 0);
    }

    /* Next emit should fail */
    primitive_event_t evt = primitive_event_create(PRIM_FREE);
    evt.description = "overflow";
    int rc = event_bus_emit(&evt);
    ASSERT_EQ(rc, -1);

    event_bus_cleanup();
}

static void test_step_numbering(void) {
    event_bus_cleanup();
    event_bus_init();
    reset_handler_state();

    event_bus_subscribe(PRIM_NONE, test_handler, NULL);

    primitive_event_t evt1 = primitive_event_create(PRIM_ALLOC);
    evt1.description = "step1";
    event_bus_emit(&evt1);
    event_bus_process();
    ASSERT_EQ(g_last_step, 1);

    primitive_event_t evt2 = primitive_event_create(PRIM_FREE);
    evt2.description = "step2";
    event_bus_emit(&evt2);
    event_bus_process();
    ASSERT_EQ(g_last_step, 2);

    event_bus_cleanup();
}

static void test_pause_resume(void) {
    event_bus_cleanup();
    event_bus_init();
    reset_handler_state();

    event_bus_subscribe(PRIM_NONE, test_handler, NULL);

    /* Pause */
    event_bus_pause();
    ASSERT_TRUE(event_bus_is_paused());

    /* Emit while paused */
    primitive_event_t evt = primitive_event_create(PRIM_ALLOC);
    evt.description = "paused";
    event_bus_emit(&evt);

    /* Process should not dispatch while paused */
    event_bus_process();
    ASSERT_EQ(g_handler_count, 0);

    /* Resume and process */
    event_bus_resume();
    ASSERT_FALSE(event_bus_is_paused());
    event_bus_process();
    ASSERT_EQ(g_handler_count, 1);

    event_bus_cleanup();
}

static bool test_filter_fn(const primitive_event_t *evt, void *userdata) {
    (void)userdata;
    return evt->type == PRIM_ALLOC; /* Only allow ALLOC */
}

static void test_global_filter(void) {
    event_bus_cleanup();
    event_bus_init();
    reset_handler_state();

    event_bus_subscribe(PRIM_NONE, test_handler, NULL);
    event_bus_set_filter(test_filter_fn, NULL);

    /* ALLOC should pass filter */
    primitive_event_t evt1 = primitive_event_create(PRIM_ALLOC);
    evt1.description = "alloc";
    event_bus_emit(&evt1);

    /* FREE should be filtered out */
    primitive_event_t evt2 = primitive_event_create(PRIM_FREE);
    evt2.description = "free";
    event_bus_emit(&evt2);

    event_bus_process();
    ASSERT_EQ(g_handler_count, 1);
    ASSERT_EQ(g_last_type, PRIM_ALLOC);

    /* Clear filter */
    event_bus_set_filter(NULL, NULL);
    event_bus_cleanup();
}

static void test_clear(void) {
    event_bus_cleanup();
    event_bus_init();

    /* Emit some events */
    for (int i = 0; i < 10; i++) {
        primitive_event_t evt = primitive_event_create(PRIM_ALLOC);
        evt.description = "clear test";
        event_bus_emit(&evt);
    }
    ASSERT_EQ(event_bus_pending_count(), 10);

    event_bus_clear();
    ASSERT_EQ(event_bus_pending_count(), 0);

    event_bus_cleanup();
}

static void test_total_events(void) {
    event_bus_cleanup();
    event_bus_init();

    for (int i = 0; i < 5; i++) {
        primitive_event_t evt = primitive_event_create(PRIM_ALLOC);
        evt.description = "count";
        event_bus_emit(&evt);
    }

    ASSERT_EQ(event_bus_total_events(), 5);

    event_bus_cleanup();
}

void test_suite_event_bus(void) {
    printf("\n  Suite: event_bus\n");
    RUN_TEST(test_init_cleanup);
    RUN_TEST(test_emit_process);
    RUN_TEST(test_subscribe_unsubscribe);
    RUN_TEST(test_type_filtering);
    RUN_TEST(test_queue_full);
    RUN_TEST(test_step_numbering);
    RUN_TEST(test_pause_resume);
    RUN_TEST(test_global_filter);
    RUN_TEST(test_clear);
    RUN_TEST(test_total_events);
}

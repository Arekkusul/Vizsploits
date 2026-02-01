#include "../src/visualization/timeline.h"
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

#define RUN_TEST(fn) do { \
    int before_fail = g_tests_failed; \
    fn(); \
    if (g_tests_failed == before_fail) { \
        printf("    PASS: %s\n", #fn); \
    } \
} while(0)

/* Tests */

static void test_create_destroy(void) {
    timeline_t *tl = timeline_create();
    ASSERT_NOT_NULL(tl);
    ASSERT_EQ(tl->num_entries, 0);
    ASSERT_EQ(tl->current_step, 0);
    ASSERT_TRUE(tl->paused);
    timeline_destroy(tl);
}

static void test_add_events(void) {
    timeline_t *tl = timeline_create();

    primitive_event_t evt1 = {0};
    evt1.type = PRIM_ALLOC;
    evt1.description = "alloc event";
    ASSERT_EQ(timeline_add_event(tl, &evt1), 0);

    primitive_event_t evt2 = {0};
    evt2.type = PRIM_FREE;
    evt2.description = "free event";
    ASSERT_EQ(timeline_add_event(tl, &evt2), 0);

    ASSERT_EQ(tl->num_entries, 2);

    /* Verify events are copied */
    ASSERT_EQ(tl->entries[0].event.type, PRIM_ALLOC);
    ASSERT_EQ(tl->entries[1].event.type, PRIM_FREE);
    /* Description should be deep-copied */
    ASSERT(tl->entries[0].event.description != evt1.description);

    timeline_destroy(tl);
}

static void test_step_forward(void) {
    timeline_t *tl = timeline_create();

    primitive_event_t evt = {0};
    evt.type = PRIM_ALLOC;
    evt.description = "event";
    timeline_add_event(tl, &evt);
    timeline_add_event(tl, &evt);
    timeline_add_event(tl, &evt);

    ASSERT_EQ(tl->current_step, 0);

    timeline_step_forward(tl);
    ASSERT_EQ(tl->current_step, 1);
    ASSERT_TRUE(tl->entries[0].completed);

    timeline_step_forward(tl);
    ASSERT_EQ(tl->current_step, 2);

    timeline_step_forward(tl);
    ASSERT_EQ(tl->current_step, 3);

    /* Should not go past end */
    timeline_step_forward(tl);
    ASSERT_EQ(tl->current_step, 3);

    timeline_destroy(tl);
}

static void test_step_backward(void) {
    timeline_t *tl = timeline_create();

    primitive_event_t evt = {0};
    evt.type = PRIM_ALLOC;
    evt.description = "event";
    timeline_add_event(tl, &evt);
    timeline_add_event(tl, &evt);

    timeline_step_forward(tl);
    timeline_step_forward(tl);
    ASSERT_EQ(tl->current_step, 2);

    timeline_step_backward(tl);
    ASSERT_EQ(tl->current_step, 1);
    ASSERT_FALSE(tl->entries[1].completed);

    timeline_step_backward(tl);
    ASSERT_EQ(tl->current_step, 0);

    /* Should not go below 0 */
    timeline_step_backward(tl);
    ASSERT_EQ(tl->current_step, 0);

    timeline_destroy(tl);
}

static void test_goto_step(void) {
    timeline_t *tl = timeline_create();

    primitive_event_t evt = {0};
    evt.type = PRIM_ALLOC;
    evt.description = "event";
    for (int i = 0; i < 10; i++) {
        timeline_add_event(tl, &evt);
    }

    timeline_goto_step(tl, 5);
    ASSERT_EQ(tl->current_step, 5);
    /* Steps 0-4 should be completed */
    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(tl->entries[i].completed);
    }
    for (int i = 5; i < 10; i++) {
        ASSERT_FALSE(tl->entries[i].completed);
    }

    /* Goto past end should clamp */
    timeline_goto_step(tl, 100);
    ASSERT_EQ(tl->current_step, 10);

    timeline_destroy(tl);
}

static void test_at_checkpoint(void) {
    timeline_t *tl = timeline_create();

    primitive_event_t evt1 = {0};
    evt1.type = PRIM_ALLOC;
    evt1.description = "alloc";
    timeline_add_event(tl, &evt1);

    primitive_event_t evt2 = {0};
    evt2.type = PRIM_CHECKPOINT;
    evt2.description = "checkpoint";
    timeline_add_event(tl, &evt2);

    /* At step 0: not checkpoint */
    ASSERT_FALSE(timeline_at_checkpoint(tl));

    /* At step 1: checkpoint */
    timeline_step_forward(tl);
    ASSERT_TRUE(timeline_at_checkpoint(tl));

    timeline_destroy(tl);
}

static void test_current(void) {
    timeline_t *tl = timeline_create();

    /* Empty timeline returns NULL */
    ASSERT_NULL(timeline_current(tl));

    primitive_event_t evt = {0};
    evt.type = PRIM_WRITE;
    evt.description = "write";
    timeline_add_event(tl, &evt);

    const timeline_entry_t *entry = timeline_current(tl);
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ(entry->event.type, PRIM_WRITE);

    /* Past end returns NULL */
    timeline_step_forward(tl);
    ASSERT_NULL(timeline_current(tl));

    timeline_destroy(tl);
}

static void test_clear(void) {
    timeline_t *tl = timeline_create();

    primitive_event_t evt = {0};
    evt.type = PRIM_ALLOC;
    evt.description = "event";
    for (int i = 0; i < 5; i++) {
        timeline_add_event(tl, &evt);
    }
    timeline_step_forward(tl);
    timeline_step_forward(tl);

    timeline_clear(tl);
    ASSERT_EQ(tl->num_entries, 0);
    ASSERT_EQ(tl->current_step, 0);
    ASSERT_EQ(tl->scroll_offset, 0);

    timeline_destroy(tl);
}

static void test_capacity_growth(void) {
    timeline_t *tl = timeline_create();

    /* Add more than initial capacity (256) */
    primitive_event_t evt = {0};
    evt.type = PRIM_ALLOC;
    evt.description = "grow";

    for (int i = 0; i < 300; i++) {
        ASSERT_EQ(timeline_add_event(tl, &evt), 0);
    }
    ASSERT_EQ(tl->num_entries, 300);

    timeline_destroy(tl);
}

void test_suite_timeline(void) {
    printf("\n  Suite: timeline\n");
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_add_events);
    RUN_TEST(test_step_forward);
    RUN_TEST(test_step_backward);
    RUN_TEST(test_goto_step);
    RUN_TEST(test_at_checkpoint);
    RUN_TEST(test_current);
    RUN_TEST(test_clear);
    RUN_TEST(test_capacity_growth);
}

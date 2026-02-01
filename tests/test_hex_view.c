#include "../src/visualization/hex_view.h"
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

/* Tests */

static void test_create_destroy(void) {
    hex_view_t *hv = hex_view_create();
    ASSERT_NOT_NULL(hv);
    ASSERT_EQ(hv->num_regions, 0);
    ASSERT_EQ(hv->bytes_per_row, 8);
    ASSERT_TRUE(hv->show_ascii);
    ASSERT_EQ(hv->selected_region, -1);
    hex_view_destroy(hv);
}

static void test_add_region(void) {
    hex_view_t *hv = hex_view_create();

    int idx = hex_view_add_region(hv, (void*)0x1000, 64, "test region");
    ASSERT_EQ(idx, 0);
    ASSERT_EQ(hv->num_regions, 1);
    ASSERT_TRUE(hv->regions[0].is_valid);
    ASSERT_EQ(hv->regions[0].size, 64);
    ASSERT(hv->regions[0].base_addr == (void*)0x1000);

    hex_view_destroy(hv);
}

static void test_find_region(void) {
    hex_view_t *hv = hex_view_create();

    hex_view_add_region(hv, (void*)0x1000, 64, "region A");
    hex_view_add_region(hv, (void*)0x2000, 128, "region B");

    ASSERT_EQ(hex_view_find_region(hv, (void*)0x1000), 0);
    ASSERT_EQ(hex_view_find_region(hv, (void*)0x2000), 1);
    ASSERT_EQ(hex_view_find_region(hv, (void*)0x3000), -1);

    hex_view_destroy(hv);
}

static void test_remove_region(void) {
    hex_view_t *hv = hex_view_create();

    hex_view_add_region(hv, (void*)0x1000, 64, "region");
    ASSERT_TRUE(hv->regions[0].is_valid);

    hex_view_remove_region(hv, 0);
    ASSERT_FALSE(hv->regions[0].is_valid);
    ASSERT_EQ(hex_view_find_region(hv, (void*)0x1000), -1);

    hex_view_destroy(hv);
}

static void test_update_data_and_state(void) {
    hex_view_t *hv = hex_view_create();

    hex_view_add_region(hv, (void*)0x1000, 64, "region");

    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    hex_view_update_region(hv, 0, 0, data, 4, BYTE_MODIFIED);

    ASSERT_EQ(hv->regions[0].data[0], 0xDE);
    ASSERT_EQ(hv->regions[0].data[1], 0xAD);
    ASSERT_EQ(hv->regions[0].data[2], 0xBE);
    ASSERT_EQ(hv->regions[0].data[3], 0xEF);

    /* BYTE_MODIFIED gets promoted to BYTE_JUST_CHANGED */
    ASSERT_EQ(hv->regions[0].state[0], BYTE_JUST_CHANGED);
    ASSERT_EQ(hv->regions[0].state[1], BYTE_JUST_CHANGED);

    hex_view_destroy(hv);
}

static void test_mark_freed(void) {
    hex_view_t *hv = hex_view_create();

    hex_view_add_region(hv, (void*)0x1000, 32, "region");
    ASSERT_FALSE(hv->regions[0].is_freed);

    hex_view_mark_freed(hv, 0);
    ASSERT_TRUE(hv->regions[0].is_freed);

    /* All bytes should be BYTE_FREED */
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ(hv->regions[0].state[i], BYTE_FREED);
    }

    hex_view_destroy(hv);
}

static void test_decay_highlights(void) {
    hex_view_t *hv = hex_view_create();

    hex_view_add_region(hv, (void*)0x1000, 16, "region");

    uint8_t data[] = {0x41, 0x42};
    hex_view_update_region(hv, 0, 0, data, 2, BYTE_MODIFIED);

    /* Should be JUST_CHANGED initially */
    ASSERT_EQ(hv->regions[0].state[0], BYTE_JUST_CHANGED);

    /* After decay, should be MODIFIED */
    hex_view_decay_highlights(hv);
    ASSERT_EQ(hv->regions[0].state[0], BYTE_MODIFIED);
    ASSERT_EQ(hv->regions[0].state[1], BYTE_MODIFIED);

    /* Second decay should keep MODIFIED */
    hex_view_decay_highlights(hv);
    ASSERT_EQ(hv->regions[0].state[0], BYTE_MODIFIED);

    hex_view_destroy(hv);
}

static void test_max_region_limit(void) {
    hex_view_t *hv = hex_view_create();

    /* Fill all regions */
    for (int i = 0; i < HEX_MAX_REGIONS; i++) {
        int idx = hex_view_add_region(hv, (void*)(uintptr_t)(0x1000 + i * 0x100), 16, "r");
        ASSERT_EQ(idx, i);
    }

    /* Next add should fail */
    int idx = hex_view_add_region(hv, (void*)0xFFFF, 16, "overflow");
    ASSERT_EQ(idx, -1);

    hex_view_destroy(hv);
}

static void test_set_state(void) {
    hex_view_t *hv = hex_view_create();
    hex_view_add_region(hv, (void*)0x1000, 32, "region");

    hex_view_set_state(hv, 0, 4, 8, BYTE_CORRUPTED);

    ASSERT_EQ(hv->regions[0].state[3], BYTE_NORMAL);
    ASSERT_EQ(hv->regions[0].state[4], BYTE_CORRUPTED);
    ASSERT_EQ(hv->regions[0].state[11], BYTE_CORRUPTED);
    ASSERT_EQ(hv->regions[0].state[12], BYTE_NORMAL);

    hex_view_destroy(hv);
}

static void test_clear(void) {
    hex_view_t *hv = hex_view_create();

    hex_view_add_region(hv, (void*)0x1000, 32, "a");
    hex_view_add_region(hv, (void*)0x2000, 64, "b");
    ASSERT_EQ(hv->num_regions, 2);

    hex_view_clear(hv);
    ASSERT_EQ(hv->num_regions, 0);
    ASSERT_EQ(hv->selected_region, -1);

    hex_view_destroy(hv);
}

static void test_add_existing_updates(void) {
    hex_view_t *hv = hex_view_create();

    hex_view_add_region(hv, (void*)0x1000, 32, "first");
    ASSERT_EQ(hv->num_regions, 1);

    /* Adding same address should update, not create new */
    int idx = hex_view_add_region(hv, (void*)0x1000, 64, "updated");
    ASSERT_EQ(idx, 0);
    ASSERT_EQ(hv->num_regions, 1);
    ASSERT_EQ(hv->regions[0].size, 64);

    hex_view_destroy(hv);
}

void test_suite_hex_view(void) {
    printf("\n  Suite: hex_view\n");
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_add_region);
    RUN_TEST(test_find_region);
    RUN_TEST(test_remove_region);
    RUN_TEST(test_update_data_and_state);
    RUN_TEST(test_mark_freed);
    RUN_TEST(test_decay_highlights);
    RUN_TEST(test_max_region_limit);
    RUN_TEST(test_set_state);
    RUN_TEST(test_clear);
    RUN_TEST(test_add_existing_updates);
}

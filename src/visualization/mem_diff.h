#ifndef MEM_DIFF_H
#define MEM_DIFF_H

#include "hex_view.h"
#include <ncurses.h>
#include <stdint.h>
#include <stdbool.h>

#define MEM_DIFF_MAX_SIZE 4096

/**
 * Per-byte diff state
 */
typedef enum {
    DIFF_SAME,
    DIFF_CHANGED,
    DIFF_ADDED,
    DIFF_REMOVED
} diff_state_t;

/**
 * Memory diff snapshot
 */
typedef struct {
    uint8_t data[MEM_DIFF_MAX_SIZE];
    size_t size;
    void *base_addr;
    const char *label;
    bool captured;
} mem_snapshot_t;

/**
 * Memory diff state
 */
typedef struct {
    mem_snapshot_t snapshot_a;
    mem_snapshot_t snapshot_b;
    diff_state_t diff[MEM_DIFF_MAX_SIZE];
    size_t diff_size;
    bool diff_computed;
    int scroll_offset;
    int bytes_per_row;
} mem_diff_t;

/**
 * Create memory diff state
 */
mem_diff_t *mem_diff_create(void);

/**
 * Destroy memory diff state
 */
void mem_diff_destroy(mem_diff_t *diff);

/**
 * Capture snapshot A from a hex_view region
 */
void mem_diff_capture_a(mem_diff_t *diff, const hex_view_t *hv, int region_idx);

/**
 * Capture snapshot B and compute diff
 */
void mem_diff_capture_b(mem_diff_t *diff, const hex_view_t *hv, int region_idx);

/**
 * Compute byte-by-byte diff between snapshots
 */
void mem_diff_compute(mem_diff_t *diff);

/**
 * Render side-by-side diff into a window
 */
void mem_diff_render(const mem_diff_t *diff, WINDOW *win);

/**
 * Reset diff state
 */
void mem_diff_reset(mem_diff_t *diff);

/**
 * Check if snapshot A is captured
 */
bool mem_diff_has_a(const mem_diff_t *diff);

/**
 * Check if diff is ready to display
 */
bool mem_diff_ready(const mem_diff_t *diff);

#endif /* MEM_DIFF_H */

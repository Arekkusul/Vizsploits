#ifndef HEAP_VIEW_H
#define HEAP_VIEW_H

#include "../core/primitives.h"
#include "hex_view.h"
#include <ncurses.h>

/**
 * Heap Chunk State
 */
typedef enum {
    CHUNK_ALLOCATED,
    CHUNK_FREED,
    CHUNK_CORRUPTED,
    CHUNK_UAF_TARGET,
    CHUNK_ATTACKER
} chunk_state_t;

/**
 * Heap Chunk with memory contents
 */
typedef struct {
    void *address;
    size_t size;
    chunk_state_t state;
    const char *description;
    uint64_t alloc_time;
    uint64_t free_time;
    bool highlight;

    // Memory contents
    uint8_t *data;           // Actual data (malloc'd)
    size_t data_capacity;    // Allocated size for data
    int hex_region_idx;      // Index in hex_view
} heap_chunk_t;

/**
 * Heap View State
 */
typedef struct {
    heap_chunk_t *chunks;
    size_t num_chunks;
    size_t capacity;
    int scroll_offset;
    int selected_chunk;

    // Hex view for detailed memory display
    hex_view_t *hex_view;

    // Display mode
    bool show_hex_detail;    // Show hex dump instead of simple view
} heap_view_t;

/**
 * Initialize heap view
 */
heap_view_t *heap_view_create(void);

/**
 * Destroy heap view
 */
void heap_view_destroy(heap_view_t *view);

/**
 * Handle primitive event (updates heap state)
 */
void heap_view_on_event(heap_view_t *view, const primitive_event_t *evt);

/**
 * Render heap view to window
 */
void heap_view_render(heap_view_t *view, WINDOW *win, bool focused);

/**
 * Clear all chunks
 */
void heap_view_clear(heap_view_t *view);

/**
 * Scroll heap view
 */
void heap_view_scroll(heap_view_t *view, int delta);

/**
 * Toggle hex detail view
 */
void heap_view_toggle_hex(heap_view_t *view);

/**
 * Select next/prev chunk
 */
void heap_view_select_next(heap_view_t *view);
void heap_view_select_prev(heap_view_t *view);

#endif // HEAP_VIEW_H

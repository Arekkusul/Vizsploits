#ifndef STACK_VIEW_H
#define STACK_VIEW_H

#include "../core/primitives.h"
#include "hex_view.h"
#include <ncurses.h>

#define STACK_VIEW_SIZE 512

/**
 * Stack variable/buffer entry
 */
typedef struct {
    const char *name;
    int offset;              // Offset from frame base (negative = local)
    size_t size;
    bool is_buffer;
    bool is_corrupted;
} stack_var_t;

/**
 * Stack Frame Entry
 */
typedef struct {
    void *return_addr;
    void *frame_ptr;
    void *saved_rbp;
    const char *func_name;
    bool is_corrupted;
    bool highlight;

    // Variables in this frame
    stack_var_t vars[16];
    size_t num_vars;
} stack_frame_t;

/**
 * Stack View State
 */
typedef struct {
    stack_frame_t *frames;
    size_t num_frames;
    size_t capacity;

    // Simulated stack memory
    uint8_t stack_data[STACK_VIEW_SIZE];
    byte_state_t stack_state[STACK_VIEW_SIZE];
    void *stack_base;        // High address (bottom of stack)
    size_t stack_used;       // Bytes used from top

    // Overflow tracking
    bool overflow_detected;
    void *overflow_start;
    size_t overflow_size;

    int scroll_offset;
    int bytes_per_row;

    // Hex view for stack memory
    hex_view_t *hex_view;
} stack_view_t;

/**
 * Create stack view
 */
stack_view_t *stack_view_create(void);

/**
 * Destroy stack view
 */
void stack_view_destroy(stack_view_t *view);

/**
 * Handle primitive event
 */
void stack_view_on_event(stack_view_t *view, const primitive_event_t *evt);

/**
 * Render stack view to window
 */
void stack_view_render(stack_view_t *view, WINDOW *win, bool focused);

/**
 * Push a stack frame
 */
void stack_view_push_frame(stack_view_t *view, const char *func_name,
                           void *return_addr, void *frame_ptr);

/**
 * Pop a stack frame
 */
void stack_view_pop_frame(stack_view_t *view);

/**
 * Add a variable to current frame
 */
void stack_view_add_var(stack_view_t *view, const char *name,
                        int offset, size_t size, bool is_buffer);

/**
 * Write data to stack (simulated)
 */
void stack_view_write(stack_view_t *view, int offset,
                      const uint8_t *data, size_t len);

/**
 * Mark overflow on stack
 */
void stack_view_mark_overflow(stack_view_t *view, void *start, size_t size);

/**
 * Clear stack view
 */
void stack_view_clear(stack_view_t *view);

/**
 * Scroll stack view
 */
void stack_view_scroll(stack_view_t *view, int delta);

#endif // STACK_VIEW_H

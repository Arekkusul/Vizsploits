#ifndef STACK_VIEW_H
#define STACK_VIEW_H

#include "../core/primitives.h"
#include <ncurses.h>

/**
 * Stack Frame Entry
 */
typedef struct {
    void *return_addr;
    void *frame_ptr;
    const char *func_name;
    bool is_corrupted;
    bool highlight;
} stack_frame_t;

/**
 * Stack View State
 */
typedef struct {
    stack_frame_t *frames;
    size_t num_frames;
    size_t capacity;

    // Simulated stack for demos
    void *stack_base;
    void *stack_ptr;
    size_t stack_size;

    // Overflow tracking
    bool overflow_detected;
    void *overflow_start;
    size_t overflow_size;

    int scroll_offset;
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
void stack_view_render(stack_view_t *view, WINDOW *win);

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
 * Mark overflow on stack
 */
void stack_view_mark_overflow(stack_view_t *view, void *start, size_t size);

/**
 * Clear stack view
 */
void stack_view_clear(stack_view_t *view);

#endif // STACK_VIEW_H

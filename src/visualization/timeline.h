#ifndef TIMELINE_H
#define TIMELINE_H

#include "../core/primitives.h"
#include <ncurses.h>

/**
 * Timeline Entry
 */
typedef struct {
    primitive_event_t event;
    bool completed;
    bool is_checkpoint;
} timeline_entry_t;

/**
 * Timeline State
 */
typedef struct {
    timeline_entry_t *entries;
    size_t num_entries;
    size_t capacity;
    size_t current_step;
    int scroll_offset;
    bool paused;
} timeline_t;

/**
 * Create timeline
 */
timeline_t *timeline_create(void);

/**
 * Destroy timeline
 */
void timeline_destroy(timeline_t *timeline);

/**
 * Add event to timeline
 */
int timeline_add_event(timeline_t *timeline, const primitive_event_t *evt);

/**
 * Render timeline to window
 */
void timeline_render(timeline_t *timeline, WINDOW *win);

/**
 * Step forward in timeline
 */
void timeline_step_forward(timeline_t *timeline);

/**
 * Step backward in timeline
 */
void timeline_step_backward(timeline_t *timeline);

/**
 * Jump to step
 */
void timeline_goto_step(timeline_t *timeline, size_t step);

/**
 * Clear timeline
 */
void timeline_clear(timeline_t *timeline);

/**
 * Get current entry
 */
const timeline_entry_t *timeline_current(const timeline_t *timeline);

/**
 * Check if at checkpoint
 */
bool timeline_at_checkpoint(const timeline_t *timeline);

#endif // TIMELINE_H

#ifndef TIMELINE_FILTER_H
#define TIMELINE_FILTER_H

#include "../core/primitives.h"
#include "timeline.h"
#include <stddef.h>
#include <stdbool.h>

#define TIMELINE_FILTER_MAX_TEXT 128

/**
 * Timeline filter state
 */
typedef struct {
    /* Type filter: PRIM_NONE means show all types */
    primitive_type_t type_filter;

    /* Text search (case-insensitive substring match on description) */
    char search_text[TIMELINE_FILTER_MAX_TEXT];
    bool search_active;

    /* Filtered index array: maps visible row -> original entry index */
    size_t *filtered_indices;
    size_t num_filtered;
    size_t filtered_capacity;
} timeline_filter_t;

/**
 * Create a new timeline filter
 */
timeline_filter_t *timeline_filter_create(void);

/**
 * Destroy a timeline filter
 */
void timeline_filter_destroy(timeline_filter_t *filter);

/**
 * Apply filter to a timeline, rebuilding filtered_indices
 */
void timeline_filter_apply(timeline_filter_t *filter, const timeline_t *timeline);

/**
 * Set text search string (empty string clears search)
 */
void timeline_filter_set_search(timeline_filter_t *filter, const char *text);

/**
 * Set type filter (PRIM_NONE = show all)
 */
void timeline_filter_set_type(timeline_filter_t *filter, primitive_type_t type);

/**
 * Cycle type filter through common types
 */
void timeline_filter_cycle_type(timeline_filter_t *filter);

/**
 * Clear all filters
 */
void timeline_filter_clear(timeline_filter_t *filter);

/**
 * Check if any filter is active
 */
bool timeline_filter_is_active(const timeline_filter_t *filter);

#endif /* TIMELINE_FILTER_H */

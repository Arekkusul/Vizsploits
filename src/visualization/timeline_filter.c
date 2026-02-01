#include "timeline_filter.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define FILTER_INITIAL_CAPACITY 256

/* Case-insensitive substring search */
static bool strcasestr_match(const char *haystack, const char *needle) {
    if (!haystack || !needle || needle[0] == '\0') return true;

    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return false;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

/* Types to cycle through with 'f' key */
static const primitive_type_t cycle_types[] = {
    PRIM_NONE,  /* All */
    PRIM_ALLOC,
    PRIM_FREE,
    PRIM_WRITE,
    PRIM_READ,
    PRIM_UAF,
    PRIM_DOUBLE_FREE,
    PRIM_OVERFLOW,
    PRIM_CALL,
    PRIM_CHECKPOINT,
};
static const size_t num_cycle_types = sizeof(cycle_types) / sizeof(cycle_types[0]);

timeline_filter_t *timeline_filter_create(void) {
    timeline_filter_t *filter = calloc(1, sizeof(timeline_filter_t));
    if (!filter) return NULL;

    filter->type_filter = PRIM_NONE;
    filter->filtered_capacity = FILTER_INITIAL_CAPACITY;
    filter->filtered_indices = calloc(filter->filtered_capacity, sizeof(size_t));
    if (!filter->filtered_indices) {
        free(filter);
        return NULL;
    }

    return filter;
}

void timeline_filter_destroy(timeline_filter_t *filter) {
    if (!filter) return;
    free(filter->filtered_indices);
    free(filter);
}

void timeline_filter_apply(timeline_filter_t *filter, const timeline_t *timeline) {
    if (!filter || !timeline) return;

    filter->num_filtered = 0;

    for (size_t i = 0; i < timeline->num_entries; i++) {
        const timeline_entry_t *entry = &timeline->entries[i];

        /* Type filter */
        if (filter->type_filter != PRIM_NONE && entry->event.type != filter->type_filter) {
            continue;
        }

        /* Text search */
        if (filter->search_active && filter->search_text[0] != '\0') {
            const char *desc = entry->event.description;
            const char *type_str = primitive_type_to_string(entry->event.type);
            const char *type_short = primitive_type_short(entry->event.type);

            if (!strcasestr_match(desc, filter->search_text) &&
                !strcasestr_match(type_str, filter->search_text) &&
                !strcasestr_match(type_short, filter->search_text)) {
                continue;
            }
        }

        /* Grow array if needed */
        if (filter->num_filtered >= filter->filtered_capacity) {
            size_t new_cap = filter->filtered_capacity * 2;
            size_t *new_arr = realloc(filter->filtered_indices, new_cap * sizeof(size_t));
            if (!new_arr) return;
            filter->filtered_indices = new_arr;
            filter->filtered_capacity = new_cap;
        }

        filter->filtered_indices[filter->num_filtered++] = i;
    }
}

void timeline_filter_set_search(timeline_filter_t *filter, const char *text) {
    if (!filter) return;

    if (text && text[0] != '\0') {
        strncpy(filter->search_text, text, TIMELINE_FILTER_MAX_TEXT - 1);
        filter->search_text[TIMELINE_FILTER_MAX_TEXT - 1] = '\0';
        filter->search_active = true;
    } else {
        filter->search_text[0] = '\0';
        filter->search_active = false;
    }
}

void timeline_filter_set_type(timeline_filter_t *filter, primitive_type_t type) {
    if (!filter) return;
    filter->type_filter = type;
}

void timeline_filter_cycle_type(timeline_filter_t *filter) {
    if (!filter) return;

    /* Find current position in cycle */
    size_t current = 0;
    for (size_t i = 0; i < num_cycle_types; i++) {
        if (cycle_types[i] == filter->type_filter) {
            current = i;
            break;
        }
    }

    /* Advance to next */
    current = (current + 1) % num_cycle_types;
    filter->type_filter = cycle_types[current];
}

void timeline_filter_clear(timeline_filter_t *filter) {
    if (!filter) return;
    filter->type_filter = PRIM_NONE;
    filter->search_text[0] = '\0';
    filter->search_active = false;
    filter->num_filtered = 0;
}

bool timeline_filter_is_active(const timeline_filter_t *filter) {
    if (!filter) return false;
    return filter->type_filter != PRIM_NONE || filter->search_active;
}

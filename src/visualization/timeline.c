#include "timeline.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 256

timeline_t *timeline_create(void) {
    timeline_t *timeline = calloc(1, sizeof(timeline_t));
    if (!timeline) return NULL;

    timeline->capacity = INITIAL_CAPACITY;
    timeline->entries = calloc(timeline->capacity, sizeof(timeline_entry_t));
    if (!timeline->entries) {
        free(timeline);
        return NULL;
    }

    timeline->paused = true;
    return timeline;
}

void timeline_destroy(timeline_t *timeline) {
    if (!timeline) return;

    // Free cloned events
    for (size_t i = 0; i < timeline->num_entries; i++) {
        free((void*)timeline->entries[i].event.description);
    }

    free(timeline->entries);
    free(timeline);
}

int timeline_add_event(timeline_t *timeline, const primitive_event_t *evt) {
    if (!timeline || !evt) return -1;

    // Grow if needed
    if (timeline->num_entries >= timeline->capacity) {
        timeline->capacity *= 2;
        timeline->entries = realloc(timeline->entries,
                                    timeline->capacity * sizeof(timeline_entry_t));
        if (!timeline->entries) return -1;
    }

    timeline_entry_t *entry = &timeline->entries[timeline->num_entries++];
    memset(entry, 0, sizeof(timeline_entry_t));

    // Copy event
    entry->event = *evt;
    if (evt->description) {
        entry->event.description = strdup(evt->description);
    }

    entry->completed = false;
    entry->is_checkpoint = (evt->type == PRIM_CHECKPOINT);

    return 0;
}

void timeline_render(timeline_t *timeline, WINDOW *win) {
    if (!timeline || !win) return;

    int height, width;
    getmaxyx(win, height, width);

    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " Timeline ");

    // Status line
    wattron(win, A_DIM);
    mvwprintw(win, 0, width - 20, " Step %zu/%zu ",
              timeline->current_step, timeline->num_entries);
    wattroff(win, A_DIM);

    if (timeline->num_entries == 0) {
        mvwprintw(win, 2, 2, "(no events)");
        wrefresh(win);
        return;
    }

    // Calculate visible range
    int visible_lines = height - 3;
    int start = timeline->scroll_offset;

    // Auto-scroll to keep current step visible
    if ((int)timeline->current_step < start) {
        start = (int)timeline->current_step;
    } else if ((int)timeline->current_step >= start + visible_lines) {
        start = (int)timeline->current_step - visible_lines + 1;
    }
    if (start < 0) start = 0;
    timeline->scroll_offset = start;

    int y = 1;
    for (size_t i = start; i < timeline->num_entries && y < height - 2; i++) {
        timeline_entry_t *entry = &timeline->entries[i];

        // Status indicator
        char status;
        int color_pair;

        if (i < timeline->current_step) {
            status = 'x';  // Completed
            color_pair = 2;  // Green
        } else if (i == timeline->current_step) {
            status = '>';  // Current
            color_pair = 3;  // Yellow
        } else {
            status = ' ';  // Pending
            color_pair = 0;  // Default
        }

        // Highlight checkpoints
        if (entry->is_checkpoint) {
            wattron(win, A_BOLD);
        }

        if (i == timeline->current_step) {
            wattron(win, A_REVERSE);
        }

        wattron(win, COLOR_PAIR(color_pair));

        // Truncate description to fit
        char desc[64];
        const char *src = entry->event.description ? entry->event.description : "";
        int max_desc = width - 18;
        if (max_desc > 60) max_desc = 60;
        snprintf(desc, sizeof(desc), "%-*.*s", max_desc, max_desc, src);

        mvwprintw(win, y, 1, " %c %3zu. %-6s %s",
                  status,
                  i + 1,
                  primitive_type_short(entry->event.type),
                  desc);

        wattroff(win, COLOR_PAIR(color_pair));

        if (i == timeline->current_step) {
            wattroff(win, A_REVERSE);
        }

        if (entry->is_checkpoint) {
            wattroff(win, A_BOLD);
        }

        y++;
    }

    // Controls hint
    mvwprintw(win, height - 1, 2, " [Space]=Step [R]=Run [B]=Back [Q]=Quit ");

    wrefresh(win);
}

void timeline_step_forward(timeline_t *timeline) {
    if (!timeline) return;

    if (timeline->current_step < timeline->num_entries) {
        timeline->entries[timeline->current_step].completed = true;
        timeline->current_step++;
    }
}

void timeline_step_backward(timeline_t *timeline) {
    if (!timeline || timeline->current_step == 0) return;

    timeline->current_step--;
    timeline->entries[timeline->current_step].completed = false;
}

void timeline_goto_step(timeline_t *timeline, size_t step) {
    if (!timeline) return;

    if (step > timeline->num_entries) {
        step = timeline->num_entries;
    }

    // Mark all before as completed
    for (size_t i = 0; i < timeline->num_entries; i++) {
        timeline->entries[i].completed = (i < step);
    }

    timeline->current_step = step;
}

void timeline_clear(timeline_t *timeline) {
    if (!timeline) return;

    for (size_t i = 0; i < timeline->num_entries; i++) {
        free((void*)timeline->entries[i].event.description);
    }

    timeline->num_entries = 0;
    timeline->current_step = 0;
    timeline->scroll_offset = 0;
}

const timeline_entry_t *timeline_current(const timeline_t *timeline) {
    if (!timeline || timeline->current_step >= timeline->num_entries) {
        return NULL;
    }
    return &timeline->entries[timeline->current_step];
}

bool timeline_at_checkpoint(const timeline_t *timeline) {
    const timeline_entry_t *entry = timeline_current(timeline);
    return entry && entry->is_checkpoint;
}

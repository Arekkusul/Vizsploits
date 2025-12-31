#include "stack_view.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 32

stack_view_t *stack_view_create(void) {
    stack_view_t *view = calloc(1, sizeof(stack_view_t));
    if (!view) return NULL;

    view->capacity = INITIAL_CAPACITY;
    view->frames = calloc(view->capacity, sizeof(stack_frame_t));
    if (!view->frames) {
        free(view);
        return NULL;
    }

    return view;
}

void stack_view_destroy(stack_view_t *view) {
    if (!view) return;
    free(view->frames);
    free(view);
}

void stack_view_on_event(stack_view_t *view, const primitive_event_t *evt) {
    if (!view || !evt) return;

    // Clear highlights
    for (size_t i = 0; i < view->num_frames; i++) {
        view->frames[i].highlight = false;
    }

    switch (evt->type) {
        case PRIM_CALL:
            stack_view_push_frame(view, evt->data.call.func_name,
                                  evt->data.call.return_addr,
                                  NULL);
            if (view->num_frames > 0) {
                view->frames[view->num_frames - 1].highlight = true;
            }
            break;

        case PRIM_RETURN:
            if (view->num_frames > 0) {
                view->frames[view->num_frames - 1].highlight = true;
            }
            stack_view_pop_frame(view);
            break;

        case PRIM_OVERFLOW:
            stack_view_mark_overflow(view, evt->data.overflow.buffer_start,
                                     evt->data.overflow.overflow_bytes);
            // Mark affected frames as corrupted
            for (size_t i = 0; i < view->num_frames; i++) {
                view->frames[i].is_corrupted = true;
                view->frames[i].highlight = true;
            }
            break;

        default:
            break;
    }
}

void stack_view_render(stack_view_t *view, WINDOW *win) {
    if (!view || !win) return;

    int height, width;
    getmaxyx(win, height, width);

    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " Stack ");

    if (view->overflow_detected) {
        wattron(win, COLOR_PAIR(1) | A_BOLD);
        mvwprintw(win, 0, width - 12, " OVERFLOW! ");
        wattroff(win, COLOR_PAIR(1) | A_BOLD);
    }

    if (view->num_frames == 0) {
        mvwprintw(win, 2, 2, "(no stack frames)");
        wrefresh(win);
        return;
    }

    int y = 1;

    // Stack grows down, so show frames from top (most recent) to bottom
    for (int i = (int)view->num_frames - 1; i >= 0 && y < height - 2; i--) {
        stack_frame_t *frame = &view->frames[i];

        int color = 0;
        if (frame->is_corrupted) {
            color = 1;  // Red
        } else if (frame->highlight) {
            color = 3;  // Yellow
        } else {
            color = 2;  // Green
        }

        if (frame->highlight) {
            wattron(win, A_BOLD);
        }

        wattron(win, COLOR_PAIR(color));

        // Draw frame
        mvwprintw(win, y++, 2, "+------------------------+");

        // Function name
        char name_buf[20];
        const char *name = frame->func_name ? frame->func_name : "???";
        snprintf(name_buf, sizeof(name_buf), "%.18s", name);
        mvwprintw(win, y++, 2, "| %-22s |", name_buf);

        // Return address
        if (frame->return_addr) {
            mvwprintw(win, y++, 2, "| ret: 0x%014lx |",
                      (unsigned long)frame->return_addr);
        }

        // Frame pointer
        if (frame->frame_ptr) {
            mvwprintw(win, y++, 2, "| rbp: 0x%014lx |",
                      (unsigned long)frame->frame_ptr);
        }

        if (frame->is_corrupted) {
            mvwprintw(win, y++, 2, "| !! CORRUPTED !!        |");
        }

        mvwprintw(win, y++, 2, "+------------------------+");

        wattroff(win, COLOR_PAIR(color));

        if (frame->highlight) {
            wattroff(win, A_BOLD);
        }
    }

    // Draw stack pointer indicator
    mvwprintw(win, height - 2, 2, "RSP -->");

    wrefresh(win);
}

void stack_view_push_frame(stack_view_t *view, const char *func_name,
                           void *return_addr, void *frame_ptr) {
    if (!view) return;

    // Grow if needed
    if (view->num_frames >= view->capacity) {
        view->capacity *= 2;
        view->frames = realloc(view->frames, view->capacity * sizeof(stack_frame_t));
        if (!view->frames) return;
    }

    stack_frame_t *frame = &view->frames[view->num_frames++];
    memset(frame, 0, sizeof(stack_frame_t));
    frame->func_name = func_name;
    frame->return_addr = return_addr;
    frame->frame_ptr = frame_ptr;
}

void stack_view_pop_frame(stack_view_t *view) {
    if (!view || view->num_frames == 0) return;
    view->num_frames--;
}

void stack_view_mark_overflow(stack_view_t *view, void *start, size_t size) {
    if (!view) return;
    view->overflow_detected = true;
    view->overflow_start = start;
    view->overflow_size = size;
}

void stack_view_clear(stack_view_t *view) {
    if (!view) return;
    view->num_frames = 0;
    view->overflow_detected = false;
    view->overflow_start = NULL;
    view->overflow_size = 0;
}

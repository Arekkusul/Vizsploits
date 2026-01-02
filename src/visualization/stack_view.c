#include "stack_view.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 32

// Color pairs
#define COLOR_RED_PAIR     1
#define COLOR_GREEN_PAIR   2
#define COLOR_YELLOW_PAIR  3
#define COLOR_BLUE_PAIR    4
#define COLOR_MAGENTA_PAIR 5
#define COLOR_CYAN_PAIR    6

// Helper to clear stack state array
static void clear_stack_state(byte_state_t *state, size_t len) {
    for (size_t i = 0; i < len; i++) {
        state[i] = BYTE_NORMAL;
    }
}

// Helper to decay highlights
static void decay_stack_state(byte_state_t *state, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (state[i] == BYTE_JUST_CHANGED) {
            state[i] = BYTE_MODIFIED;
        }
    }
}

stack_view_t *stack_view_create(void) {
    stack_view_t *view = calloc(1, sizeof(stack_view_t));
    if (!view) return NULL;

    view->capacity = INITIAL_CAPACITY;
    view->frames = calloc(view->capacity, sizeof(stack_frame_t));
    if (!view->frames) {
        free(view);
        return NULL;
    }

    view->hex_view = hex_view_create();
    view->bytes_per_row = 8;

    view->stack_base = (void*)0x7fffffffe000UL;
    memset(view->stack_data, 0, STACK_VIEW_SIZE);
    clear_stack_state(view->stack_state, STACK_VIEW_SIZE);

    return view;
}

void stack_view_destroy(stack_view_t *view) {
    if (!view) return;
    free(view->frames);
    hex_view_destroy(view->hex_view);
    free(view);
}

void stack_view_on_event(stack_view_t *view, const primitive_event_t *evt) {
    if (!view || !evt) return;

    // Clear highlights and decay states
    for (size_t i = 0; i < view->num_frames; i++) {
        view->frames[i].highlight = false;
    }
    decay_stack_state(view->stack_state, STACK_VIEW_SIZE);

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
            for (size_t i = 0; i < view->num_frames; i++) {
                view->frames[i].is_corrupted = true;
                view->frames[i].highlight = true;
            }

            if (view->stack_used > 0) {
                size_t corrupt_start = evt->data.overflow.buffer_size;
                size_t corrupt_len = evt->data.overflow.overflow_bytes;
                if (corrupt_start + corrupt_len <= STACK_VIEW_SIZE) {
                    for (size_t i = 0; i < corrupt_len; i++) {
                        view->stack_state[corrupt_start + i] = BYTE_CORRUPTED;
                    }
                }
            }
            break;

        case PRIM_WRITE: {
            if (evt->data.memop.preview[0] != 0 || evt->size > 0) {
                size_t len = evt->data.memop.len;
                if (len > 16) len = 16;

                if (view->stack_used < STACK_VIEW_SIZE) {
                    size_t offset = view->stack_used;
                    if (offset + len > STACK_VIEW_SIZE) {
                        len = STACK_VIEW_SIZE - offset;
                    }
                    memcpy(view->stack_data + offset, evt->data.memop.preview, len);
                    for (size_t i = 0; i < len; i++) {
                        view->stack_state[offset + i] = BYTE_JUST_CHANGED;
                    }
                    view->stack_used += len;
                }
            }
            break;
        }

        default:
            break;
    }
}

void stack_view_render(stack_view_t *view, WINDOW *win, bool focused) {
    if (!view || !win) return;

    int height, width;
    getmaxyx(win, height, width);

    werase(win);
    box(win, 0, 0);

    // Title
    if (focused) wattron(win, A_REVERSE);
    mvwprintw(win, 0, 2, " Stack ");
    if (focused) wattroff(win, A_REVERSE);

    if (view->overflow_detected) {
        wattron(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD | A_BLINK);
        mvwprintw(win, 0, width - 12, " OVERFLOW! ");
        wattroff(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD | A_BLINK);
    }

    int mid_x = width / 2;

    // Separator
    for (int i = 1; i < height - 1; i++) {
        mvwaddch(win, i, mid_x - 1, ACS_VLINE);
    }

    // === Left side: Frame visualization ===
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 2, "Frames");
    wattroff(win, A_BOLD);

    wattron(win, A_DIM);
    wprintw(win, " (%zu)", view->num_frames);
    wattroff(win, A_DIM);

    if (view->num_frames == 0) {
        wattron(win, A_DIM);
        mvwprintw(win, 3, 2, "(no frames)");
        wattroff(win, A_DIM);
    } else {
        int y = 3;
        int frame_start = view->scroll_offset;
        int max_frames = (height - 5) / 3;

        for (int i = (int)view->num_frames - 1 - frame_start;
             i >= 0 && (int)(view->num_frames - 1 - frame_start - i) < max_frames;
             i--) {
            stack_frame_t *frame = &view->frames[i];

            int color = frame->is_corrupted ? COLOR_RED_PAIR :
                       frame->highlight ? COLOR_YELLOW_PAIR : COLOR_GREEN_PAIR;

            if (frame->highlight) wattron(win, A_BOLD);
            wattron(win, COLOR_PAIR(color));

            // Compact frame box
            const char *name = frame->func_name ? frame->func_name : "???";
            mvwprintw(win, y++, 2, "+----------------+");
            mvwprintw(win, y++, 2, "|%-16.16s|", name);

            if (frame->return_addr) {
                if (frame->is_corrupted) {
                    mvwprintw(win, y++, 2, "|ret: CORRUPTED! |");
                } else {
                    mvwprintw(win, y++, 2, "|ret:%012lx|",
                              (unsigned long)frame->return_addr & 0xFFFFFFFFFFFFUL);
                }
            }

            wattroff(win, COLOR_PAIR(color));
            if (frame->highlight) wattroff(win, A_BOLD);
        }

        // RSP indicator
        wattron(win, COLOR_PAIR(COLOR_CYAN_PAIR));
        mvwprintw(win, height - 2, 2, "RSP ->");
        wattroff(win, COLOR_PAIR(COLOR_CYAN_PAIR));
    }

    // === Right side: Hex dump ===
    int hex_x = mid_x + 1;
    int hex_y = 1;

    wattron(win, A_BOLD);
    mvwprintw(win, hex_y++, hex_x, "Memory");
    wattroff(win, A_BOLD);

    wattron(win, A_DIM);
    mvwprintw(win, hex_y++, hex_x, "base: %04lx",
              (unsigned long)view->stack_base & 0xFFFF);
    wattroff(win, A_DIM);
    hex_y++;

    // Hex dump
    int bytes_per_row = 8;
    int hex_width = width - hex_x - 2;
    if (hex_width < 45) bytes_per_row = 4;

    int max_rows = height - hex_y - 2;
    int row_start = view->scroll_offset;

    for (int row = 0; row < max_rows; row++) {
        int data_row = row + row_start;
        size_t offset = data_row * bytes_per_row;

        if (offset >= STACK_VIEW_SIZE || offset >= view->stack_used + 64) {
            break;
        }

        size_t len = STACK_VIEW_SIZE - offset;
        if (len > (size_t)bytes_per_row) len = bytes_per_row;

        uint32_t display_addr = (uint32_t)((unsigned long)view->stack_base - offset - bytes_per_row);

        hex_render_line_short(win, hex_y + row, hex_x,
                              display_addr, view->stack_data + offset,
                              view->stack_state + offset, len,
                              true, bytes_per_row);
    }

    // Help
    wattron(win, A_DIM);
    mvwprintw(win, height - 1, width - 14, " [j/k] scroll ");
    wattroff(win, A_DIM);

    wrefresh(win);
}

void stack_view_push_frame(stack_view_t *view, const char *func_name,
                           void *return_addr, void *frame_ptr) {
    if (!view) return;

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

    if (view->stack_used + 8 <= STACK_VIEW_SIZE && return_addr) {
        uint64_t addr = (uint64_t)return_addr;
        memcpy(view->stack_data + view->stack_used, &addr, 8);
        for (int i = 0; i < 8; i++) {
            view->stack_state[view->stack_used + i] = BYTE_POINTER;
        }
        view->stack_used += 8;
    }
}

void stack_view_pop_frame(stack_view_t *view) {
    if (!view || view->num_frames == 0) return;
    view->num_frames--;

    if (view->stack_used >= 8) {
        view->stack_used -= 8;
    }
}

void stack_view_add_var(stack_view_t *view, const char *name,
                        int offset, size_t size, bool is_buffer) {
    if (!view || view->num_frames == 0) return;

    stack_frame_t *frame = &view->frames[view->num_frames - 1];
    if (frame->num_vars >= 16) return;

    stack_var_t *var = &frame->vars[frame->num_vars++];
    var->name = name;
    var->offset = offset;
    var->size = size;
    var->is_buffer = is_buffer;
    var->is_corrupted = false;
}

void stack_view_write(stack_view_t *view, int offset,
                      const uint8_t *data, size_t len) {
    if (!view || !data) return;

    size_t pos = view->stack_used + offset;
    if (pos >= STACK_VIEW_SIZE) return;

    size_t copy_len = len;
    if (pos + copy_len > STACK_VIEW_SIZE) {
        copy_len = STACK_VIEW_SIZE - pos;
    }

    memcpy(view->stack_data + pos, data, copy_len);
    for (size_t i = 0; i < copy_len; i++) {
        view->stack_state[pos + i] = BYTE_JUST_CHANGED;
    }
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
    view->scroll_offset = 0;
    view->stack_used = 0;
    memset(view->stack_data, 0, STACK_VIEW_SIZE);
    clear_stack_state(view->stack_state, STACK_VIEW_SIZE);

    hex_view_clear(view->hex_view);
}

void stack_view_scroll(stack_view_t *view, int delta) {
    if (!view) return;

    view->scroll_offset += delta;
    if (view->scroll_offset < 0) {
        view->scroll_offset = 0;
    }

    int max_scroll = (STACK_VIEW_SIZE / view->bytes_per_row) - 5;
    if (view->scroll_offset > max_scroll) {
        view->scroll_offset = max_scroll;
    }
}

#include "hex_view.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Color pairs (must match tui.c)
#define COLOR_RED_PAIR     1
#define COLOR_GREEN_PAIR   2
#define COLOR_YELLOW_PAIR  3
#define COLOR_BLUE_PAIR    4
#define COLOR_MAGENTA_PAIR 5
#define COLOR_CYAN_PAIR    6
#define COLOR_WHITE_PAIR   7

hex_view_t *hex_view_create(void) {
    hex_view_t *view = calloc(1, sizeof(hex_view_t));
    if (!view) return NULL;

    view->bytes_per_row = 8;  // Default for narrower windows
    view->show_ascii = true;
    view->selected_region = -1;
    view->cursor_offset = -1;

    return view;
}

void hex_view_destroy(hex_view_t *view) {
    if (!view) return;
    free(view);
}

int hex_view_add_region(hex_view_t *view, void *addr, size_t size, const char *label) {
    if (!view || view->num_regions >= HEX_MAX_REGIONS) return -1;

    // Check if region already exists
    int existing = hex_view_find_region(view, addr);
    if (existing >= 0) {
        // Update existing region
        hex_region_t *region = &view->regions[existing];
        if (size > HEX_MAX_REGION_SIZE) size = HEX_MAX_REGION_SIZE;
        region->size = size;
        region->label = label;
        region->is_valid = true;
        region->is_freed = false;
        memset(region->state, BYTE_NORMAL, size);
        return existing;
    }

    // Create new region
    hex_region_t *region = &view->regions[view->num_regions];
    memset(region, 0, sizeof(hex_region_t));

    region->base_addr = addr;
    region->capacity = HEX_MAX_REGION_SIZE;
    region->size = (size > HEX_MAX_REGION_SIZE) ? HEX_MAX_REGION_SIZE : size;
    region->label = label;
    region->is_valid = true;
    region->is_freed = false;
    region->highlight_start = -1;
    region->highlight_end = -1;

    // Initialize with zeros
    memset(region->data, 0, region->size);
    memset(region->state, BYTE_NORMAL, region->size);

    return (int)view->num_regions++;
}

void hex_view_update_region(hex_view_t *view, int region_idx,
                            size_t offset, const uint8_t *data, size_t len,
                            byte_state_t state) {
    if (!view || region_idx < 0 || region_idx >= (int)view->num_regions) return;

    hex_region_t *region = &view->regions[region_idx];
    if (!region->is_valid) return;

    // Clamp to region bounds
    if (offset >= region->size) return;
    if (offset + len > region->size) {
        len = region->size - offset;
    }

    // Copy data
    if (data) {
        memcpy(region->data + offset, data, len);
    }

    // Set state - use JUST_CHANGED for immediate visual feedback
    byte_state_t apply_state = (state == BYTE_MODIFIED) ? BYTE_JUST_CHANGED : state;
    for (size_t i = 0; i < len; i++) {
        region->state[offset + i] = apply_state;
    }
}

void hex_view_set_state(hex_view_t *view, int region_idx,
                        size_t offset, size_t len, byte_state_t state) {
    if (!view || region_idx < 0 || region_idx >= (int)view->num_regions) return;

    hex_region_t *region = &view->regions[region_idx];
    if (!region->is_valid) return;

    if (offset >= region->size) return;
    if (offset + len > region->size) {
        len = region->size - offset;
    }

    for (size_t i = 0; i < len; i++) {
        region->state[offset + i] = state;
    }
}

void hex_view_mark_freed(hex_view_t *view, int region_idx) {
    if (!view || region_idx < 0 || region_idx >= (int)view->num_regions) return;

    hex_region_t *region = &view->regions[region_idx];
    region->is_freed = true;
    for (size_t i = 0; i < region->size; i++) {
        region->state[i] = BYTE_FREED;
    }
}

void hex_view_remove_region(hex_view_t *view, int region_idx) {
    if (!view || region_idx < 0 || region_idx >= (int)view->num_regions) return;
    view->regions[region_idx].is_valid = false;
}

int hex_view_find_region(hex_view_t *view, void *addr) {
    if (!view) return -1;

    for (size_t i = 0; i < view->num_regions; i++) {
        if (view->regions[i].is_valid && view->regions[i].base_addr == addr) {
            return (int)i;
        }
    }
    return -1;
}

void hex_view_decay_highlights(hex_view_t *view) {
    if (!view) return;

    for (size_t r = 0; r < view->num_regions; r++) {
        hex_region_t *region = &view->regions[r];
        if (!region->is_valid) continue;

        for (size_t i = 0; i < region->size; i++) {
            if (region->state[i] == BYTE_JUST_CHANGED) {
                region->state[i] = BYTE_MODIFIED;
            }
        }
    }
}

static int get_color_for_state(byte_state_t state) {
    switch (state) {
        case BYTE_JUST_CHANGED: return COLOR_GREEN_PAIR;   // Bright green for just changed
        case BYTE_MODIFIED:     return COLOR_YELLOW_PAIR;  // Yellow for modified
        case BYTE_CORRUPTED:    return COLOR_RED_PAIR;     // Red for corrupted
        case BYTE_FREED:        return COLOR_BLUE_PAIR;    // Blue for freed
        case BYTE_POINTER:      return COLOR_CYAN_PAIR;    // Cyan for pointers
        case BYTE_SHELLCODE:    return COLOR_MAGENTA_PAIR; // Magenta for shellcode
        default:                return 0;
    }
}

static attr_t get_attr_for_state(byte_state_t state) {
    switch (state) {
        case BYTE_JUST_CHANGED: return A_BOLD | A_REVERSE;  // Very visible
        case BYTE_MODIFIED:     return A_BOLD;
        case BYTE_CORRUPTED:    return A_BOLD | A_BLINK;
        case BYTE_FREED:        return A_DIM;
        default:                return 0;
    }
}

int hex_render_line_short(WINDOW *win, int y, int x,
                          uint32_t addr_low, const uint8_t *data,
                          const byte_state_t *states, size_t len,
                          bool show_ascii, int bytes_per_row) {
    if (!win || !data) return 0;

    int start_x = x;

    // Short address (4 hex digits)
    wattron(win, A_DIM);
    mvwprintw(win, y, x, "%04x ", addr_low & 0xFFFF);
    wattroff(win, A_DIM);
    x += 5;

    // Hex bytes with spacing
    for (int i = 0; i < bytes_per_row; i++) {
        if ((size_t)i < len) {
            byte_state_t st = states ? states[i] : BYTE_NORMAL;
            int color = get_color_for_state(st);
            attr_t attr = get_attr_for_state(st);

            if (color) wattron(win, COLOR_PAIR(color));
            if (attr) wattron(win, attr);

            mvwprintw(win, y, x, "%02x", data[i]);

            if (attr) wattroff(win, attr);
            if (color) wattroff(win, COLOR_PAIR(color));
        } else {
            mvwprintw(win, y, x, "  ");
        }
        x += 2;

        // Space between bytes, extra space at midpoint
        if (i == bytes_per_row / 2 - 1) {
            mvwaddch(win, y, x++, ' ');
        }
        mvwaddch(win, y, x++, ' ');
    }

    // ASCII column
    if (show_ascii) {
        wattron(win, A_DIM);
        mvwaddch(win, y, x++, '|');
        wattroff(win, A_DIM);

        for (int i = 0; i < bytes_per_row; i++) {
            if ((size_t)i < len) {
                char c = (char)data[i];
                byte_state_t st = states ? states[i] : BYTE_NORMAL;
                int color = get_color_for_state(st);

                if (color) wattron(win, COLOR_PAIR(color));

                if (c >= 32 && c < 127) {
                    waddch(win, c);
                } else {
                    waddch(win, '.');
                }

                if (color) wattroff(win, COLOR_PAIR(color));
            } else {
                waddch(win, ' ');
            }
        }

        wattron(win, A_DIM);
        waddch(win, '|');
        wattroff(win, A_DIM);
        x += bytes_per_row + 1;
    }

    return x - start_x;
}

int hex_render_line(WINDOW *win, int y, int x,
                    void *display_addr, const uint8_t *data,
                    const byte_state_t *states, size_t len,
                    bool show_ascii, int bytes_per_row) {
    // Use short format for compact display
    uint32_t addr_low = (uint32_t)((unsigned long)display_addr & 0xFFFFFFFF);
    return hex_render_line_short(win, y, x, addr_low, data, states, len, show_ascii, bytes_per_row);
}

void hex_view_render_region(hex_view_t *view, WINDOW *win,
                            int region_idx, int start_y, int max_lines,
                            bool show_header) {
    if (!view || !win || region_idx < 0 || region_idx >= (int)view->num_regions) return;

    hex_region_t *region = &view->regions[region_idx];
    if (!region->is_valid) return;

    int y = start_y;
    int width;
    int height;
    getmaxyx(win, height, width);
    (void)height;

    // Render header
    if (show_header && region->label) {
        wattron(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));
        mvwprintw(win, y, 1, " %s @ %04lx ",
                  region->label, (unsigned long)region->base_addr & 0xFFFF);
        wattroff(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));

        // Status indicator
        if (region->is_freed) {
            wattron(win, COLOR_PAIR(COLOR_BLUE_PAIR));
            wprintw(win, "[FREED]");
            wattroff(win, COLOR_PAIR(COLOR_BLUE_PAIR));
        }

        y++;
        max_lines--;
    }

    // Calculate how many lines we need
    int total_lines = (region->size + view->bytes_per_row - 1) / view->bytes_per_row;
    int start_line = view->scroll_offset;

    if (start_line >= total_lines) start_line = total_lines - 1;
    if (start_line < 0) start_line = 0;

    // Render hex lines
    for (int line = start_line; line < total_lines && max_lines > 0; line++, y++, max_lines--) {
        size_t offset = line * view->bytes_per_row;
        size_t line_len = region->size - offset;
        if (line_len > (size_t)view->bytes_per_row) line_len = view->bytes_per_row;

        uint32_t line_addr = (uint32_t)((unsigned long)region->base_addr + offset);

        hex_render_line_short(win, y, 1, line_addr,
                              region->data + offset,
                              region->state + offset,
                              line_len, view->show_ascii, view->bytes_per_row);
    }
}

void hex_view_render(hex_view_t *view, WINDOW *win, bool focused) {
    if (!view || !win) return;

    int height, width;
    getmaxyx(win, height, width);
    (void)width;

    werase(win);
    box(win, 0, 0);

    // Title with focus indicator
    if (focused) {
        wattron(win, A_REVERSE);
    }
    mvwprintw(win, 0, 2, " Hex View ");
    if (focused) {
        wattroff(win, A_REVERSE);
    }

    if (view->num_regions == 0) {
        mvwprintw(win, 2, 2, "(no memory regions)");
        wrefresh(win);
        return;
    }

    // Render all valid regions
    int y = 1;
    int remaining = height - 3;

    for (size_t i = 0; i < view->num_regions && remaining > 0; i++) {
        if (!view->regions[i].is_valid) continue;

        // Calculate lines needed for this region
        int region_lines = (view->regions[i].size + view->bytes_per_row - 1) / view->bytes_per_row;
        region_lines += 1;  // Header

        if (region_lines > remaining) region_lines = remaining;

        hex_view_render_region(view, win, i, y, region_lines, true);
        y += region_lines + 1;  // +1 for spacing
        remaining -= region_lines + 1;
    }

    wrefresh(win);
}

void hex_view_scroll(hex_view_t *view, int delta) {
    if (!view) return;

    view->scroll_offset += delta;
    if (view->scroll_offset < 0) view->scroll_offset = 0;
}

void hex_view_clear(hex_view_t *view) {
    if (!view) return;

    for (size_t i = 0; i < view->num_regions; i++) {
        view->regions[i].is_valid = false;
    }
    view->num_regions = 0;
    view->scroll_offset = 0;
    view->selected_region = -1;
}

void hex_view_set_highlight(hex_view_t *view, int region_idx,
                            int start, int end) {
    if (!view || region_idx < 0 || region_idx >= (int)view->num_regions) return;

    view->regions[region_idx].highlight_start = start;
    view->regions[region_idx].highlight_end = end;
}

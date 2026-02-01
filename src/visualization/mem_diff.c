#include "mem_diff.h"
#include <stdlib.h>
#include <string.h>

/* Color pairs (must match tui.c) */
#define COLOR_RED_PAIR     1
#define COLOR_GREEN_PAIR   2
#define COLOR_YELLOW_PAIR  3
#define COLOR_BLUE_PAIR    4
#define COLOR_CYAN_PAIR    6

mem_diff_t *mem_diff_create(void) {
    mem_diff_t *diff = calloc(1, sizeof(mem_diff_t));
    if (!diff) return NULL;
    diff->bytes_per_row = 8;
    return diff;
}

void mem_diff_destroy(mem_diff_t *diff) {
    free(diff);
}

static void capture_snapshot(mem_snapshot_t *snap, const hex_view_t *hv, int region_idx) {
    if (!snap || !hv || region_idx < 0 || region_idx >= (int)hv->num_regions) return;

    const hex_region_t *region = &hv->regions[region_idx];
    if (!region->is_valid) return;

    size_t copy_size = region->size;
    if (copy_size > MEM_DIFF_MAX_SIZE) copy_size = MEM_DIFF_MAX_SIZE;

    memcpy(snap->data, region->data, copy_size);
    snap->size = copy_size;
    snap->base_addr = region->base_addr;
    snap->label = region->label;
    snap->captured = true;
}

void mem_diff_capture_a(mem_diff_t *diff, const hex_view_t *hv, int region_idx) {
    if (!diff) return;
    capture_snapshot(&diff->snapshot_a, hv, region_idx);
    diff->snapshot_b.captured = false;
    diff->diff_computed = false;
}

void mem_diff_capture_b(mem_diff_t *diff, const hex_view_t *hv, int region_idx) {
    if (!diff) return;
    capture_snapshot(&diff->snapshot_b, hv, region_idx);
    if (diff->snapshot_a.captured) {
        mem_diff_compute(diff);
    }
}

void mem_diff_compute(mem_diff_t *diff) {
    if (!diff || !diff->snapshot_a.captured || !diff->snapshot_b.captured) return;

    size_t size_a = diff->snapshot_a.size;
    size_t size_b = diff->snapshot_b.size;
    size_t max_size = size_a > size_b ? size_a : size_b;
    if (max_size > MEM_DIFF_MAX_SIZE) max_size = MEM_DIFF_MAX_SIZE;

    diff->diff_size = max_size;

    for (size_t i = 0; i < max_size; i++) {
        if (i >= size_a) {
            diff->diff[i] = DIFF_ADDED;
        } else if (i >= size_b) {
            diff->diff[i] = DIFF_REMOVED;
        } else if (diff->snapshot_a.data[i] != diff->snapshot_b.data[i]) {
            diff->diff[i] = DIFF_CHANGED;
        } else {
            diff->diff[i] = DIFF_SAME;
        }
    }

    diff->diff_computed = true;
}

void mem_diff_render(const mem_diff_t *diff, WINDOW *win) {
    if (!diff || !win || !diff->diff_computed) return;

    int height, width;
    getmaxyx(win, height, width);

    werase(win);
    box(win, 0, 0);

    wattron(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));
    mvwprintw(win, 0, 2, " Memory Diff ");
    wattroff(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));

    int y = 1;
    int bpr = diff->bytes_per_row;

    /* Header: labels */
    wattron(win, A_DIM);
    mvwprintw(win, y, 2, "A: %s", diff->snapshot_a.label ? diff->snapshot_a.label : "snapshot");
    mvwprintw(win, y, width / 2 + 1, "B: %s", diff->snapshot_b.label ? diff->snapshot_b.label : "snapshot");
    wattroff(win, A_DIM);
    y += 2;

    /* Count changes */
    int changed = 0;
    for (size_t i = 0; i < diff->diff_size; i++) {
        if (diff->diff[i] != DIFF_SAME) changed++;
    }
    mvwprintw(win, y - 1, 2, "%d bytes differ", changed);

    /* Render side-by-side hex */
    int half = width / 2 - 1;
    size_t offset = diff->scroll_offset * bpr;

    for (; offset < diff->diff_size && y < height - 1; offset += bpr, y++) {
        /* Left side: snapshot A */
        int x = 2;
        wattron(win, A_DIM);
        mvwprintw(win, y, x, "%04x ", (unsigned)(uintptr_t)diff->snapshot_a.base_addr + (unsigned)offset);
        wattroff(win, A_DIM);
        x += 5;

        for (int i = 0; i < bpr && offset + i < diff->diff_size; i++) {
            size_t idx = offset + i;
            diff_state_t st = diff->diff[idx];

            if (st == DIFF_CHANGED) {
                wattron(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            } else if (st == DIFF_REMOVED) {
                wattron(win, COLOR_PAIR(COLOR_YELLOW_PAIR));
            }

            if (idx < diff->snapshot_a.size) {
                mvwprintw(win, y, x, "%02x", diff->snapshot_a.data[idx]);
            } else {
                mvwprintw(win, y, x, "--");
            }
            x += 3;

            if (st == DIFF_CHANGED) {
                wattroff(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            } else if (st == DIFF_REMOVED) {
                wattroff(win, COLOR_PAIR(COLOR_YELLOW_PAIR));
            }
        }

        /* Divider */
        mvwaddch(win, y, half, ACS_VLINE);

        /* Right side: snapshot B */
        x = half + 2;
        wattron(win, A_DIM);
        mvwprintw(win, y, x, "%04x ", (unsigned)(uintptr_t)diff->snapshot_b.base_addr + (unsigned)offset);
        wattroff(win, A_DIM);
        x += 5;

        for (int i = 0; i < bpr && offset + i < diff->diff_size; i++) {
            size_t idx = offset + i;
            diff_state_t st = diff->diff[idx];

            if (st == DIFF_CHANGED) {
                wattron(win, COLOR_PAIR(COLOR_GREEN_PAIR) | A_BOLD);
            } else if (st == DIFF_ADDED) {
                wattron(win, COLOR_PAIR(COLOR_BLUE_PAIR));
            }

            if (idx < diff->snapshot_b.size) {
                mvwprintw(win, y, x, "%02x", diff->snapshot_b.data[idx]);
            } else {
                mvwprintw(win, y, x, "--");
            }
            x += 3;

            if (st == DIFF_CHANGED) {
                wattroff(win, COLOR_PAIR(COLOR_GREEN_PAIR) | A_BOLD);
            } else if (st == DIFF_ADDED) {
                wattroff(win, COLOR_PAIR(COLOR_BLUE_PAIR));
            }
        }
    }

    wrefresh(win);
}

void mem_diff_reset(mem_diff_t *diff) {
    if (!diff) return;
    diff->snapshot_a.captured = false;
    diff->snapshot_b.captured = false;
    diff->diff_computed = false;
    diff->diff_size = 0;
    diff->scroll_offset = 0;
}

bool mem_diff_has_a(const mem_diff_t *diff) {
    return diff && diff->snapshot_a.captured;
}

bool mem_diff_ready(const mem_diff_t *diff) {
    return diff && diff->diff_computed;
}

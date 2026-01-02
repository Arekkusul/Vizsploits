#include "heap_view.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 64
#define CHUNK_DATA_SIZE 256

// Color pairs (must match tui.c)
#define COLOR_RED_PAIR     1
#define COLOR_GREEN_PAIR   2
#define COLOR_YELLOW_PAIR  3
#define COLOR_BLUE_PAIR    4
#define COLOR_MAGENTA_PAIR 5
#define COLOR_CYAN_PAIR    6

heap_view_t *heap_view_create(void) {
    heap_view_t *view = calloc(1, sizeof(heap_view_t));
    if (!view) return NULL;

    view->capacity = INITIAL_CAPACITY;
    view->chunks = calloc(view->capacity, sizeof(heap_chunk_t));
    if (!view->chunks) {
        free(view);
        return NULL;
    }

    view->hex_view = hex_view_create();
    view->selected_chunk = -1;
    view->show_hex_detail = true;

    return view;
}

void heap_view_destroy(heap_view_t *view) {
    if (!view) return;

    for (size_t i = 0; i < view->num_chunks; i++) {
        free(view->chunks[i].data);
    }

    free(view->chunks);
    hex_view_destroy(view->hex_view);
    free(view);
}

static heap_chunk_t *find_chunk(heap_view_t *view, void *addr) {
    for (size_t i = 0; i < view->num_chunks; i++) {
        if (view->chunks[i].address == addr) {
            return &view->chunks[i];
        }
    }
    return NULL;
}

static int find_chunk_index(heap_view_t *view, void *addr) {
    for (size_t i = 0; i < view->num_chunks; i++) {
        if (view->chunks[i].address == addr) {
            return (int)i;
        }
    }
    return -1;
}

static heap_chunk_t *add_chunk(heap_view_t *view) {
    if (view->num_chunks >= view->capacity) {
        view->capacity *= 2;
        view->chunks = realloc(view->chunks, view->capacity * sizeof(heap_chunk_t));
        if (!view->chunks) return NULL;
    }
    heap_chunk_t *chunk = &view->chunks[view->num_chunks++];
    memset(chunk, 0, sizeof(heap_chunk_t));
    return chunk;
}

static heap_chunk_t *find_chunk_containing(heap_view_t *view, void *addr) {
    for (size_t i = 0; i < view->num_chunks; i++) {
        void *start = view->chunks[i].address;
        void *end = (char*)start + view->chunks[i].size;
        if (addr >= start && addr < end) {
            return &view->chunks[i];
        }
    }
    return NULL;
}

void heap_view_on_event(heap_view_t *view, const primitive_event_t *evt) {
    if (!view || !evt) return;

    // Clear all highlights and decay states
    for (size_t i = 0; i < view->num_chunks; i++) {
        view->chunks[i].highlight = false;
    }
    hex_view_decay_highlights(view->hex_view);

    switch (evt->type) {
        case PRIM_ALLOC: {
            heap_chunk_t *chunk = add_chunk(view);
            if (chunk) {
                chunk->address = evt->data.alloc.ptr;
                chunk->size = evt->data.alloc.requested_size;
                chunk->state = CHUNK_ALLOCATED;
                chunk->description = evt->description;
                chunk->alloc_time = evt->timestamp;
                chunk->highlight = true;

                size_t data_size = chunk->size;
                if (data_size > CHUNK_DATA_SIZE) data_size = CHUNK_DATA_SIZE;
                chunk->data = calloc(1, data_size);
                chunk->data_capacity = data_size;

                char label[32];
                snprintf(label, sizeof(label), "chunk_%zu", view->num_chunks);
                chunk->hex_region_idx = hex_view_add_region(
                    view->hex_view, chunk->address, data_size,
                    chunk->description ? chunk->description : label
                );

                view->selected_chunk = (int)view->num_chunks - 1;
            }
            break;
        }

        case PRIM_FREE: {
            heap_chunk_t *chunk = find_chunk(view, evt->data.free.ptr);
            if (chunk) {
                if (chunk->state == CHUNK_FREED) {
                    chunk->state = CHUNK_CORRUPTED;  // Double free!
                } else {
                    chunk->state = CHUNK_FREED;
                }
                chunk->free_time = evt->timestamp;
                chunk->highlight = true;

                if (chunk->hex_region_idx >= 0) {
                    hex_view_mark_freed(view->hex_view, chunk->hex_region_idx);
                }
            }
            break;
        }

        case PRIM_UAF: {
            heap_chunk_t *chunk = find_chunk(view, evt->data.uaf.freed_ptr);
            if (chunk) {
                chunk->state = CHUNK_UAF_TARGET;
                chunk->highlight = true;
            }
            break;
        }

        case PRIM_WRITE: {
            heap_chunk_t *chunk = find_chunk_containing(view, evt->data.memop.dst);
            if (chunk) {
                chunk->highlight = true;

                size_t offset = (size_t)((char*)evt->data.memop.dst - (char*)chunk->address);
                size_t len = evt->data.memop.len;

                if (chunk->data && offset < chunk->data_capacity) {
                    size_t copy_len = len;
                    if (offset + copy_len > chunk->data_capacity) {
                        copy_len = chunk->data_capacity - offset;
                    }
                    if (evt->data.memop.preview[0] != 0 || len <= 16) {
                        size_t preview_len = len > 16 ? 16 : len;
                        if (preview_len > copy_len) preview_len = copy_len;
                        memcpy(chunk->data + offset, evt->data.memop.preview, preview_len);
                    }
                }

                if (chunk->hex_region_idx >= 0) {
                    hex_view_update_region(view->hex_view, chunk->hex_region_idx,
                                           offset, evt->data.memop.preview,
                                           len > 16 ? 16 : len, BYTE_MODIFIED);
                }

                int idx = find_chunk_index(view, chunk->address);
                if (idx >= 0) view->selected_chunk = idx;
            }
            break;
        }

        case PRIM_OVERFLOW: {
            heap_chunk_t *chunk = find_chunk_containing(view, evt->data.overflow.buffer_start);
            if (chunk && chunk->hex_region_idx >= 0) {
                size_t offset = (size_t)((char*)evt->data.overflow.buffer_start - (char*)chunk->address);
                hex_view_set_state(view->hex_view, chunk->hex_region_idx,
                                   offset + evt->data.overflow.buffer_size,
                                   evt->data.overflow.overflow_bytes, BYTE_CORRUPTED);
            }
            break;
        }

        default:
            break;
    }
}

static const char *state_to_string(chunk_state_t state) {
    switch (state) {
        case CHUNK_ALLOCATED:  return "ALLOC";
        case CHUNK_FREED:      return "FREE";
        case CHUNK_CORRUPTED:  return "CORRUPT";
        case CHUNK_UAF_TARGET: return "UAF";
        case CHUNK_ATTACKER:   return "ATTACK";
        default:               return "???";
    }
}

static int state_to_color(chunk_state_t state) {
    switch (state) {
        case CHUNK_ALLOCATED:  return COLOR_GREEN_PAIR;
        case CHUNK_FREED:      return COLOR_BLUE_PAIR;
        case CHUNK_CORRUPTED:  return COLOR_RED_PAIR;
        case CHUNK_UAF_TARGET: return COLOR_RED_PAIR;
        case CHUNK_ATTACKER:   return COLOR_MAGENTA_PAIR;
        default:               return 0;
    }
}

void heap_view_render(heap_view_t *view, WINDOW *win, bool focused) {
    if (!view || !win) return;

    int height, width;
    getmaxyx(win, height, width);

    werase(win);
    box(win, 0, 0);

    // Title
    if (focused) wattron(win, A_REVERSE);
    mvwprintw(win, 0, 2, " Heap ");
    if (focused) wattroff(win, A_REVERSE);

    wattron(win, A_DIM);
    wprintw(win, " %zu chunks", view->num_chunks);
    wattroff(win, A_DIM);

    if (view->num_chunks == 0) {
        wattron(win, A_DIM);
        mvwprintw(win, height/2, (width-16)/2, "(no allocations)");
        wattroff(win, A_DIM);
        wrefresh(win);
        return;
    }

    // Layout: chunk list on left (28 chars), hex on right
    int list_width = 28;
    int hex_start_x = list_width + 1;

    // Vertical separator
    for (int i = 1; i < height - 1; i++) {
        mvwaddch(win, i, list_width, ACS_VLINE);
    }

    // Column headers
    wattron(win, A_DIM);
    mvwprintw(win, 1, 2, "ADDR     SIZE STATE");
    wattroff(win, A_DIM);

    // Chunk list
    int visible_start = view->scroll_offset;
    int visible_count = height - 4;

    for (int i = visible_start; i < (int)view->num_chunks && (i - visible_start) < visible_count; i++) {
        heap_chunk_t *chunk = &view->chunks[i];
        int row = 2 + (i - visible_start);

        int color = state_to_color(chunk->state);
        bool is_selected = (i == view->selected_chunk);

        if (is_selected) wattron(win, A_REVERSE);
        if (chunk->highlight) wattron(win, A_BOLD);
        wattron(win, COLOR_PAIR(color));

        mvwprintw(win, row, 1, "%c", is_selected ? '>' : ' ');
        wprintw(win, "%04lx %4zu %-7s",
                (unsigned long)chunk->address & 0xFFFF,
                chunk->size,
                state_to_string(chunk->state));

        wattroff(win, COLOR_PAIR(color));
        if (chunk->highlight) wattroff(win, A_BOLD);
        if (is_selected) wattroff(win, A_REVERSE);
    }

    // Scroll info
    if (view->num_chunks > (size_t)visible_count) {
        wattron(win, A_DIM);
        mvwprintw(win, height - 2, 2, "[%d/%zu]", view->scroll_offset + 1, view->num_chunks);
        wattroff(win, A_DIM);
    }

    // Hex view of selected chunk
    if (view->selected_chunk >= 0 && view->selected_chunk < (int)view->num_chunks) {
        heap_chunk_t *chunk = &view->chunks[view->selected_chunk];

        // Header
        wattron(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));
        mvwprintw(win, 1, hex_start_x + 1, "@ 0x%04lx",
                  (unsigned long)chunk->address & 0xFFFF);
        wattroff(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));

        // State badge
        int scolor = state_to_color(chunk->state);
        wattron(win, COLOR_PAIR(scolor) | A_BOLD);
        wprintw(win, " [%s]", state_to_string(chunk->state));
        wattroff(win, COLOR_PAIR(scolor) | A_BOLD);

        // Description
        if (chunk->description) {
            wattron(win, A_DIM);
            mvwprintw(win, 2, hex_start_x + 1, "%.25s", chunk->description);
            wattroff(win, A_DIM);
        }

        // Hex dump
        if (chunk->data && chunk->hex_region_idx >= 0) {
            int hex_y = 4;
            int bytes_per_row = 8;
            int max_rows = height - 6;
            int hex_width = width - hex_start_x - 2;

            // Adjust bytes per row based on available width
            if (hex_width < 45) bytes_per_row = 4;

            hex_region_t *region = &view->hex_view->regions[chunk->hex_region_idx];

            for (int row = 0; row < max_rows && (size_t)(row * bytes_per_row) < region->size; row++) {
                size_t offset = row * bytes_per_row;
                size_t len = region->size - offset;
                if (len > (size_t)bytes_per_row) len = bytes_per_row;

                uint32_t line_addr = (uint32_t)((unsigned long)region->base_addr + offset);
                hex_render_line_short(win, hex_y + row, hex_start_x + 1,
                                      line_addr, region->data + offset,
                                      region->state + offset, len,
                                      true, bytes_per_row);
            }
        }
    }

    // Help
    wattron(win, A_DIM);
    mvwprintw(win, height - 1, width - 14, " [j/k] scroll ");
    wattroff(win, A_DIM);

    wrefresh(win);
}

void heap_view_clear(heap_view_t *view) {
    if (!view) return;

    for (size_t i = 0; i < view->num_chunks; i++) {
        free(view->chunks[i].data);
        view->chunks[i].data = NULL;
    }

    view->num_chunks = 0;
    view->scroll_offset = 0;
    view->selected_chunk = -1;

    hex_view_clear(view->hex_view);
}

void heap_view_scroll(heap_view_t *view, int delta) {
    if (!view) return;

    view->selected_chunk += delta;

    if (view->selected_chunk < 0) {
        view->selected_chunk = 0;
    }
    if (view->selected_chunk >= (int)view->num_chunks) {
        view->selected_chunk = (int)view->num_chunks - 1;
    }
    if (view->selected_chunk < 0) {
        view->selected_chunk = -1;
    }

    if (view->selected_chunk >= 0) {
        if (view->selected_chunk < view->scroll_offset) {
            view->scroll_offset = view->selected_chunk;
        }
        if (view->selected_chunk > view->scroll_offset + 15) {
            view->scroll_offset = view->selected_chunk - 15;
        }
    }
}

void heap_view_toggle_hex(heap_view_t *view) {
    if (!view) return;
    view->show_hex_detail = !view->show_hex_detail;
}

void heap_view_select_next(heap_view_t *view) {
    heap_view_scroll(view, 1);
}

void heap_view_select_prev(heap_view_t *view) {
    heap_view_scroll(view, -1);
}

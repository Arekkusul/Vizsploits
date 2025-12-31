#include "heap_view.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 64

heap_view_t *heap_view_create(void) {
    heap_view_t *view = calloc(1, sizeof(heap_view_t));
    if (!view) return NULL;

    view->capacity = INITIAL_CAPACITY;
    view->chunks = calloc(view->capacity, sizeof(heap_chunk_t));
    if (!view->chunks) {
        free(view);
        return NULL;
    }

    view->selected_chunk = -1;
    return view;
}

void heap_view_destroy(heap_view_t *view) {
    if (!view) return;
    free(view->chunks);
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

static heap_chunk_t *add_chunk(heap_view_t *view) {
    if (view->num_chunks >= view->capacity) {
        view->capacity *= 2;
        view->chunks = realloc(view->chunks, view->capacity * sizeof(heap_chunk_t));
        if (!view->chunks) return NULL;
    }
    return &view->chunks[view->num_chunks++];
}

void heap_view_on_event(heap_view_t *view, const primitive_event_t *evt) {
    if (!view || !evt) return;

    // Clear all highlights first
    for (size_t i = 0; i < view->num_chunks; i++) {
        view->chunks[i].highlight = false;
    }

    switch (evt->type) {
        case PRIM_ALLOC: {
            heap_chunk_t *chunk = add_chunk(view);
            if (chunk) {
                memset(chunk, 0, sizeof(heap_chunk_t));
                chunk->address = evt->data.alloc.ptr;
                chunk->size = evt->data.alloc.requested_size;
                chunk->state = CHUNK_ALLOCATED;
                chunk->description = evt->description;
                chunk->alloc_time = evt->timestamp;
                chunk->highlight = true;
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
            // Highlight written chunk
            for (size_t i = 0; i < view->num_chunks; i++) {
                void *start = view->chunks[i].address;
                void *end = (char*)start + view->chunks[i].size;
                if (evt->data.memop.dst >= start && evt->data.memop.dst < end) {
                    view->chunks[i].highlight = true;
                    break;
                }
            }
            break;
        }

        default:
            break;
    }
}

void heap_view_render(heap_view_t *view, WINDOW *win) {
    if (!view || !win) return;

    int height, width;
    getmaxyx(win, height, width);

    // Clear and draw border
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " Heap Chunks ");

    if (view->num_chunks == 0) {
        mvwprintw(win, 2, 2, "(no allocations)");
        wrefresh(win);
        return;
    }

    int y = 1;
    int visible_chunks = (height - 2) / 6;  // Each chunk takes ~6 lines

    for (size_t i = view->scroll_offset;
         i < view->num_chunks && y < height - 6;
         i++) {
        heap_chunk_t *chunk = &view->chunks[i];

        // Choose color based on state
        int color_pair;
        const char *state_str;

        switch (chunk->state) {
            case CHUNK_ALLOCATED:
                color_pair = 2;  // Green
                state_str = "ALLOCATED";
                break;
            case CHUNK_FREED:
                color_pair = 3;  // Yellow
                state_str = "FREED";
                break;
            case CHUNK_CORRUPTED:
                color_pair = 1;  // Red
                state_str = "CORRUPTED!";
                break;
            case CHUNK_UAF_TARGET:
                color_pair = 1;  // Red
                state_str = "UAF TARGET!";
                break;
            case CHUNK_ATTACKER:
                color_pair = 5;  // Magenta
                state_str = "ATTACKER";
                break;
            default:
                color_pair = 0;
                state_str = "UNKNOWN";
                break;
        }

        if (chunk->highlight) {
            wattron(win, A_BOLD);
        }

        wattron(win, COLOR_PAIR(color_pair));

        // Draw chunk box
        mvwprintw(win, y++, 2, "+---------------------------+");
        mvwprintw(win, y++, 2, "| Addr: 0x%014lx  |", (unsigned long)chunk->address);
        mvwprintw(win, y++, 2, "| Size: %-18zu |", chunk->size);
        mvwprintw(win, y++, 2, "| [%-23s] |", state_str);
        mvwprintw(win, y++, 2, "+---------------------------+");

        wattroff(win, COLOR_PAIR(color_pair));

        if (chunk->highlight) {
            wattroff(win, A_BOLD);
        }

        y++;  // Spacing
    }

    // Scroll indicator
    if (view->num_chunks > (size_t)visible_chunks) {
        mvwprintw(win, height - 1, width - 10, " %d/%zu ",
                  view->scroll_offset + 1, view->num_chunks);
    }

    wrefresh(win);
}

void heap_view_clear(heap_view_t *view) {
    if (!view) return;
    view->num_chunks = 0;
    view->scroll_offset = 0;
    view->selected_chunk = -1;
}

void heap_view_scroll(heap_view_t *view, int delta) {
    if (!view) return;

    int new_offset = view->scroll_offset + delta;
    if (new_offset < 0) new_offset = 0;
    if (new_offset >= (int)view->num_chunks) {
        new_offset = (int)view->num_chunks - 1;
    }
    if (new_offset < 0) new_offset = 0;

    view->scroll_offset = new_offset;
}

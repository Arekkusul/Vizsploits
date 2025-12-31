#include "tui.h"
#include "../core/event_bus.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// Global for signal handler
static tui_t *g_tui = NULL;

// Color pairs
#define COLOR_RED_PAIR     1
#define COLOR_GREEN_PAIR   2
#define COLOR_YELLOW_PAIR  3
#define COLOR_BLUE_PAIR    4
#define COLOR_MAGENTA_PAIR 5
#define COLOR_CYAN_PAIR    6

static void handle_resize(int sig) {
    (void)sig;
    if (g_tui) {
        endwin();
        refresh();
        tui_handle_resize(g_tui);
    }
}

static void init_colors(void) {
    if (has_colors()) {
        start_color();
        use_default_colors();

        init_pair(COLOR_RED_PAIR, COLOR_RED, -1);
        init_pair(COLOR_GREEN_PAIR, COLOR_GREEN, -1);
        init_pair(COLOR_YELLOW_PAIR, COLOR_YELLOW, -1);
        init_pair(COLOR_BLUE_PAIR, COLOR_BLUE, -1);
        init_pair(COLOR_MAGENTA_PAIR, COLOR_MAGENTA, -1);
        init_pair(COLOR_CYAN_PAIR, COLOR_CYAN, -1);
    }
}

static void create_windows(tui_t *tui) {
    int h = tui->term_height;
    int w = tui->term_width;

    // Layout:
    // +--------------+--------------+---------------+
    // |              |              |               |
    // |   Heap View  |  Stack View  |  Info/Details |
    // |   (1/3 w)    |   (1/3 w)    |    (1/3 w)    |
    // |              |              |               |
    // +--------------+--------------+---------------+
    // |                                             |
    // |              Timeline (bottom)              |
    // |                                             |
    // +---------------------------------------------+
    // | Status bar                                  |
    // +---------------------------------------------+

    int col_w = w / 3;
    int main_h = h * 2 / 3;
    int timeline_h = h - main_h - 1;

    tui->heap_win = newwin(main_h, col_w, 0, 0);
    tui->stack_win = newwin(main_h, col_w, 0, col_w);
    tui->info_win = newwin(main_h, w - 2 * col_w, 0, 2 * col_w);
    tui->timeline_win = newwin(timeline_h, w, main_h, 0);
    tui->status_win = newwin(1, w, h - 1, 0);

    // Enable keypad for all windows
    keypad(tui->heap_win, TRUE);
    keypad(tui->stack_win, TRUE);
    keypad(tui->info_win, TRUE);
    keypad(tui->timeline_win, TRUE);
}

static void destroy_windows(tui_t *tui) {
    if (tui->heap_win) delwin(tui->heap_win);
    if (tui->stack_win) delwin(tui->stack_win);
    if (tui->info_win) delwin(tui->info_win);
    if (tui->timeline_win) delwin(tui->timeline_win);
    if (tui->status_win) delwin(tui->status_win);
    tui->heap_win = NULL;
    tui->stack_win = NULL;
    tui->info_win = NULL;
    tui->timeline_win = NULL;
    tui->status_win = NULL;
}

tui_t *tui_init(void) {
    tui_t *tui = calloc(1, sizeof(tui_t));
    if (!tui) return NULL;

    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    timeout(100);  // 100ms timeout for getch()

    init_colors();

    getmaxyx(stdscr, tui->term_height, tui->term_width);
    create_windows(tui);

    // Create views
    tui->heap_view = heap_view_create();
    tui->stack_view = stack_view_create();
    tui->timeline = timeline_create();

    tui->mode = UI_MODE_MENU;
    tui->step_mode = true;

    // Setup signal handler for resize
    g_tui = tui;
    signal(SIGWINCH, handle_resize);

    return tui;
}

void tui_cleanup(tui_t *tui) {
    if (!tui) return;

    g_tui = NULL;
    signal(SIGWINCH, SIG_DFL);

    destroy_windows(tui);

    if (tui->heap_view) heap_view_destroy(tui->heap_view);
    if (tui->stack_view) stack_view_destroy(tui->stack_view);
    if (tui->timeline) timeline_destroy(tui->timeline);

    endwin();
    free(tui);
}

void tui_handle_resize(tui_t *tui) {
    getmaxyx(stdscr, tui->term_height, tui->term_width);
    destroy_windows(tui);
    create_windows(tui);
    tui_refresh(tui);
}

static void render_status_bar(tui_t *tui) {
    werase(tui->status_win);
    wattron(tui->status_win, A_REVERSE);

    const char *mode_str = "";
    switch (tui->mode) {
        case UI_MODE_MENU:     mode_str = "MENU"; break;
        case UI_MODE_RUNNING:  mode_str = "RUNNING"; break;
        case UI_MODE_FINISHED: mode_str = "FINISHED"; break;
        case UI_MODE_HELP:     mode_str = "HELP"; break;
    }

    mvwprintw(tui->status_win, 0, 0, " Kernel Exploit Visualizer ");
    wprintw(tui->status_win, "| Mode: %s ", mode_str);

    if (tui->current_exploit) {
        wprintw(tui->status_win, "| Exploit: %s ", tui->current_exploit->meta.name);
    }

    // Fill rest of line
    int y, x;
    getyx(tui->status_win, y, x);
    (void)y;
    for (int i = x; i < tui->term_width; i++) {
        waddch(tui->status_win, ' ');
    }

    wattroff(tui->status_win, A_REVERSE);
    wrefresh(tui->status_win);
}

static void render_menu(tui_t *tui) {
    WINDOW *win = tui->info_win;
    werase(win);
    box(win, 0, 0);

    int height, width;
    getmaxyx(win, height, width);
    (void)width;  // Suppress unused warning

    // Title
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 2, "KERNEL EXPLOIT VISUALIZER");
    wattroff(win, A_BOLD);
    mvwprintw(win, 2, 2, "==========================");

    mvwprintw(win, 4, 2, "Select an exploit to visualize:");

    int y = 6;
    int count = exploit_count();

    if (count == 0) {
        mvwprintw(win, y, 4, "(no exploits registered)");
    } else {
        for (int i = 0; i < count && y < height - 4; i++) {
            exploit_t *exp = exploit_get_by_index(i);
            if (!exp) continue;

            if (i == tui->selected_exploit) {
                wattron(win, A_REVERSE);
            }

            mvwprintw(win, y, 4, "%d. %s", i + 1, exp->meta.name);

            if (i == tui->selected_exploit) {
                wattroff(win, A_REVERSE);
            }

            y++;

            // Description
            if (exp->meta.description) {
                wattron(win, A_DIM);
                mvwprintw(win, y, 7, "%.50s", exp->meta.description);
                wattroff(win, A_DIM);
                y++;
            }
            y++;
        }
    }

    // Controls
    mvwprintw(win, height - 3, 2, "Controls:");
    mvwprintw(win, height - 2, 4, "[Up/Down] Select  [Enter] Run  [Q] Quit");

    wrefresh(win);

    // Show placeholder in heap window
    werase(tui->heap_win);
    box(tui->heap_win, 0, 0);
    mvwprintw(tui->heap_win, 0, 2, " Heap ");
    mvwprintw(tui->heap_win, 2, 2, "(select exploit)");
    wrefresh(tui->heap_win);

    // Stack placeholder
    werase(tui->stack_win);
    box(tui->stack_win, 0, 0);
    mvwprintw(tui->stack_win, 0, 2, " Stack ");
    mvwprintw(tui->stack_win, 2, 2, "(select exploit)");
    wrefresh(tui->stack_win);

    // Timeline placeholder
    werase(tui->timeline_win);
    box(tui->timeline_win, 0, 0);
    mvwprintw(tui->timeline_win, 0, 2, " Timeline ");
    mvwprintw(tui->timeline_win, 2, 2, "(select an exploit to see timeline)");
    wrefresh(tui->timeline_win);
}

static void render_info(tui_t *tui) {
    WINDOW *win = tui->info_win;
    werase(win);
    box(win, 0, 0);

    int height, width;
    getmaxyx(win, height, width);
    (void)width;

    mvwprintw(win, 0, 2, " Event Details ");

    const timeline_entry_t *entry = timeline_current(tui->timeline);
    if (!entry) {
        mvwprintw(win, 2, 2, "(no current event)");
        wrefresh(win);
        return;
    }

    const primitive_event_t *evt = &entry->event;

    int y = 2;

    // Event type
    wattron(win, A_BOLD);
    mvwprintw(win, y++, 2, "Type: %s", primitive_type_to_string(evt->type));
    wattroff(win, A_BOLD);
    y++;

    // Step number
    mvwprintw(win, y++, 2, "Step: %d", evt->step_number);

    // Address
    if (evt->address) {
        mvwprintw(win, y++, 2, "Address: 0x%lx", (unsigned long)evt->address);
    }

    // Size
    if (evt->size > 0) {
        mvwprintw(win, y++, 2, "Size: %zu bytes", evt->size);
    }

    y++;

    // Description
    if (evt->description) {
        wattron(win, COLOR_PAIR(COLOR_CYAN_PAIR));
        mvwprintw(win, y++, 2, "Description:");
        wattroff(win, COLOR_PAIR(COLOR_CYAN_PAIR));

        // Word wrap description
        const char *desc = evt->description;
        int max_width = width - 6;
        int desc_y = y;

        while (*desc && desc_y < height - 4) {
            int len = strlen(desc);
            if (len > max_width) len = max_width;

            mvwprintw(win, desc_y++, 4, "%.*s", len, desc);
            desc += len;
        }
        y = desc_y + 1;
    }

    // Type-specific info
    y++;
    switch (evt->type) {
        case PRIM_ALLOC:
            mvwprintw(win, y++, 2, "Pointer: 0x%lx",
                      (unsigned long)evt->data.alloc.ptr);
            mvwprintw(win, y++, 2, "Requested: %zu bytes",
                      evt->data.alloc.requested_size);
            break;

        case PRIM_FREE:
            mvwprintw(win, y++, 2, "Pointer: 0x%lx",
                      (unsigned long)evt->data.free.ptr);
            if (evt->data.free.was_freed) {
                wattron(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
                mvwprintw(win, y++, 2, "WARNING: Already freed!");
                wattroff(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            }
            break;

        case PRIM_UAF:
            wattron(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            mvwprintw(win, y++, 2, "USE-AFTER-FREE DETECTED!");
            wattroff(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            mvwprintw(win, y++, 2, "Freed ptr: 0x%lx",
                      (unsigned long)evt->data.uaf.freed_ptr);
            break;

        case PRIM_WRITE:
            mvwprintw(win, y++, 2, "Dest: 0x%lx",
                      (unsigned long)evt->data.memop.dst);
            mvwprintw(win, y++, 2, "Length: %zu", evt->data.memop.len);
            break;

        default:
            break;
    }

    // Controls at bottom
    mvwprintw(win, height - 2, 2, "[Space] Step [R] Run [Q] Quit");

    wrefresh(win);
}

void tui_refresh(tui_t *tui) {
    switch (tui->mode) {
        case UI_MODE_MENU:
            render_menu(tui);
            break;

        case UI_MODE_RUNNING:
        case UI_MODE_FINISHED:
            heap_view_render(tui->heap_view, tui->heap_win);
            stack_view_render(tui->stack_view, tui->stack_win);
            timeline_render(tui->timeline, tui->timeline_win);
            render_info(tui);
            break;

        case UI_MODE_HELP:
            // TODO: help screen
            break;
    }

    render_status_bar(tui);
    refresh();
}

void tui_on_event(const primitive_event_t *evt, void *userdata) {
    tui_t *tui = userdata;
    if (!tui || !evt) return;

    // Add to timeline
    timeline_add_event(tui->timeline, evt);

    // Update views
    heap_view_on_event(tui->heap_view, evt);
    stack_view_on_event(tui->stack_view, evt);
}

static void run_exploit_step(tui_t *tui) {
    if (tui->timeline->current_step < tui->timeline->num_entries) {
        // Get current event and update views
        const timeline_entry_t *entry = timeline_current(tui->timeline);
        if (entry) {
            heap_view_on_event(tui->heap_view, &entry->event);
            stack_view_on_event(tui->stack_view, &entry->event);
        }
        timeline_step_forward(tui->timeline);
        tui_refresh(tui);
    } else {
        tui->mode = UI_MODE_FINISHED;
    }
}

static void start_exploit(tui_t *tui) {
    exploit_t *exp = exploit_get_by_index(tui->selected_exploit);
    if (!exp) return;

    tui->current_exploit = exp;
    tui->mode = UI_MODE_RUNNING;

    // Clear views
    heap_view_clear(tui->heap_view);
    stack_view_clear(tui->stack_view);
    timeline_clear(tui->timeline);

    // Subscribe to events
    event_bus_subscribe(PRIM_NONE, tui_on_event, tui);

    // Run the exploit (it emits events to the bus)
    if (exp->setup) exp->setup(exp);
    if (exp->run) exp->run(exp);
    if (exp->cleanup) exp->cleanup(exp);

    // Process all events
    event_bus_process();

    // Reset timeline to beginning for step-through
    timeline_goto_step(tui->timeline, 0);

    tui_refresh(tui);
}

int tui_run(tui_t *tui) {
    tui_refresh(tui);

    while (!tui->quit_requested) {
        int ch = getch();

        switch (tui->mode) {
            case UI_MODE_MENU:
                switch (ch) {
                    case 'q':
                    case 'Q':
                        tui->quit_requested = true;
                        break;

                    case KEY_UP:
                    case 'k':
                        if (tui->selected_exploit > 0) {
                            tui->selected_exploit--;
                            tui_refresh(tui);
                        }
                        break;

                    case KEY_DOWN:
                    case 'j':
                        if (tui->selected_exploit < exploit_count() - 1) {
                            tui->selected_exploit++;
                            tui_refresh(tui);
                        }
                        break;

                    case '\n':
                    case KEY_ENTER:
                        start_exploit(tui);
                        break;

                    case '?':
                        tui->mode = UI_MODE_HELP;
                        tui_refresh(tui);
                        break;
                }
                break;

            case UI_MODE_RUNNING:
            case UI_MODE_FINISHED:
                switch (ch) {
                    case 'q':
                    case 'Q':
                        // Back to menu
                        tui->mode = UI_MODE_MENU;
                        tui->current_exploit = NULL;
                        tui_refresh(tui);
                        break;

                    case ' ':
                        // Step forward
                        run_exploit_step(tui);
                        break;

                    case 'b':
                    case 'B':
                        // Step backward
                        timeline_step_backward(tui->timeline);
                        tui_refresh(tui);
                        break;

                    case 'r':
                    case 'R':
                        // Run to end (or next checkpoint)
                        while (tui->timeline->current_step < tui->timeline->num_entries) {
                            run_exploit_step(tui);
                            if (timeline_at_checkpoint(tui->timeline)) {
                                break;
                            }
                        }
                        break;

                    case '0':
                        // Reset to beginning
                        timeline_goto_step(tui->timeline, 0);
                        heap_view_clear(tui->heap_view);
                        tui_refresh(tui);
                        break;

                    case KEY_UP:
                        heap_view_scroll(tui->heap_view, -1);
                        tui_refresh(tui);
                        break;

                    case KEY_DOWN:
                        heap_view_scroll(tui->heap_view, 1);
                        tui_refresh(tui);
                        break;
                }
                break;

            case UI_MODE_HELP:
                if (ch == 'q' || ch == '?' || ch == 27) {  // ESC
                    tui->mode = UI_MODE_MENU;
                    tui_refresh(tui);
                }
                break;
        }
    }

    return 0;
}

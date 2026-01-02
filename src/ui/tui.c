#include "tui.h"
#include "../core/event_bus.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static tui_t *g_tui = NULL;

// Color pairs
#define COLOR_RED_PAIR     1
#define COLOR_GREEN_PAIR   2
#define COLOR_YELLOW_PAIR  3
#define COLOR_BLUE_PAIR    4
#define COLOR_MAGENTA_PAIR 5
#define COLOR_CYAN_PAIR    6
#define COLOR_WHITE_PAIR   7

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
        init_pair(COLOR_WHITE_PAIR, COLOR_WHITE, -1);
    }
}

static void create_windows(tui_t *tui) {
    int h = tui->term_height;
    int w = tui->term_width;

    // Layout (improved):
    // +------------+------------+------------------+
    // |   Heap     |   Stack    |   Event Details  |
    // |   (30%)    |   (30%)    |      (40%)       |
    // +------------+------------+------------------+
    // |              Timeline (20%)                |
    // +--------------------------------------------+
    // | Status bar (1 line)                        |
    // +--------------------------------------------+

    int heap_w = w * 30 / 100;
    int stack_w = w * 30 / 100;
    int info_w = w - heap_w - stack_w;

    int main_h = h * 65 / 100;
    int timeline_h = h - main_h - 2;
    if (timeline_h < 6) timeline_h = 6;
    main_h = h - timeline_h - 2;

    tui->heap_win = newwin(main_h, heap_w, 0, 0);
    tui->stack_win = newwin(main_h, stack_w, 0, heap_w);
    tui->info_win = newwin(main_h, info_w, 0, heap_w + stack_w);
    tui->timeline_win = newwin(timeline_h, w, main_h, 0);
    tui->status_win = newwin(2, w, h - 2, 0);

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

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    timeout(100);

    init_colors();

    getmaxyx(stdscr, tui->term_height, tui->term_width);
    create_windows(tui);

    tui->heap_view = heap_view_create();
    tui->stack_view = stack_view_create();
    tui->timeline = timeline_create();

    tui->mode = UI_MODE_MENU;
    tui->step_mode = true;
    tui->focused_panel = PANEL_HEAP;

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

void tui_focus_next(tui_t *tui) {
    if (!tui) return;
    tui->focused_panel = (tui->focused_panel + 1) % PANEL_COUNT;
}

void tui_focus_prev(tui_t *tui) {
    if (!tui) return;
    tui->focused_panel = (tui->focused_panel + PANEL_COUNT - 1) % PANEL_COUNT;
}

static const char *panel_name(panel_focus_t panel) {
    switch (panel) {
        case PANEL_HEAP:     return "HEAP";
        case PANEL_STACK:    return "STACK";
        case PANEL_INFO:     return "INFO";
        case PANEL_TIMELINE: return "TIMELINE";
        default:             return "???";
    }
}

static void render_status_bar(tui_t *tui) {
    int height, width;
    getmaxyx(tui->status_win, height, width);
    (void)height;

    werase(tui->status_win);

    // Line 1: Controls
    wattron(tui->status_win, A_REVERSE);

    const char *mode_str = "";
    switch (tui->mode) {
        case UI_MODE_MENU:     mode_str = "MENU"; break;
        case UI_MODE_RUNNING:  mode_str = "RUNNING"; break;
        case UI_MODE_FINISHED: mode_str = "FINISHED"; break;
        case UI_MODE_HELP:     mode_str = "HELP"; break;
    }

    mvwprintw(tui->status_win, 0, 0, " [Space]=Step [R]=Run [B]=Back [Q]=Quit ");
    wprintw(tui->status_win, "| %s ", mode_str);

    if (tui->mode == UI_MODE_RUNNING || tui->mode == UI_MODE_FINISHED) {
        wprintw(tui->status_win, "| Focus: %s ", panel_name(tui->focused_panel));
    }

    if (tui->current_exploit) {
        wprintw(tui->status_win, "| %s ", tui->current_exploit->meta.name);
    }

    // Fill line
    int y, x;
    getyx(tui->status_win, y, x);
    (void)y;
    for (int i = x; i < width; i++) waddch(tui->status_win, ' ');
    wattroff(tui->status_win, A_REVERSE);

    // Line 2: Color legend
    mvwprintw(tui->status_win, 1, 0, " Legend: ");

    wattron(tui->status_win, COLOR_PAIR(COLOR_GREEN_PAIR) | A_BOLD);
    wprintw(tui->status_win, "NEW ");
    wattroff(tui->status_win, COLOR_PAIR(COLOR_GREEN_PAIR) | A_BOLD);

    wattron(tui->status_win, COLOR_PAIR(COLOR_YELLOW_PAIR));
    wprintw(tui->status_win, "MOD ");
    wattroff(tui->status_win, COLOR_PAIR(COLOR_YELLOW_PAIR));

    wattron(tui->status_win, COLOR_PAIR(COLOR_RED_PAIR));
    wprintw(tui->status_win, "CORRUPT ");
    wattroff(tui->status_win, COLOR_PAIR(COLOR_RED_PAIR));

    wattron(tui->status_win, COLOR_PAIR(COLOR_BLUE_PAIR));
    wprintw(tui->status_win, "FREED ");
    wattroff(tui->status_win, COLOR_PAIR(COLOR_BLUE_PAIR));

    wattron(tui->status_win, COLOR_PAIR(COLOR_CYAN_PAIR));
    wprintw(tui->status_win, "PTR ");
    wattroff(tui->status_win, COLOR_PAIR(COLOR_CYAN_PAIR));

    wprintw(tui->status_win, "| [</>] panel [j/k] scroll [0] reset");

    wrefresh(tui->status_win);
}

static void render_menu(tui_t *tui) {
    WINDOW *win = tui->info_win;
    werase(win);
    box(win, 0, 0);

    int height, width;
    getmaxyx(win, height, width);
    (void)width;

    wattron(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));
    mvwprintw(win, 1, 2, "KERNEL EXPLOIT VISUALIZER");
    wattroff(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));

    mvwprintw(win, 3, 2, "Select an exploit:");

    int y = 5;
    int count = exploit_count();

    if (count == 0) {
        wattron(win, A_DIM);
        mvwprintw(win, y, 4, "(no exploits registered)");
        wattroff(win, A_DIM);
    } else {
        for (int i = 0; i < count && y < height - 4; i++) {
            exploit_t *exp = exploit_get_by_index(i);
            if (!exp) continue;

            if (i == tui->selected_exploit) {
                wattron(win, A_REVERSE | COLOR_PAIR(COLOR_CYAN_PAIR));
            }

            mvwprintw(win, y, 3, " %d. %s ", i + 1, exp->meta.name);

            if (i == tui->selected_exploit) {
                wattroff(win, A_REVERSE | COLOR_PAIR(COLOR_CYAN_PAIR));
            }
            y++;

            if (exp->meta.description) {
                wattron(win, A_DIM);
                mvwprintw(win, y, 6, "%.45s", exp->meta.description);
                wattroff(win, A_DIM);
                y++;
            }
            y++;
        }
    }

    wattron(win, A_DIM);
    mvwprintw(win, height - 2, 2, "[Up/Down] Select  [Enter] Run  [Q] Quit");
    wattroff(win, A_DIM);

    wrefresh(win);

    // Placeholders
    werase(tui->heap_win);
    box(tui->heap_win, 0, 0);
    mvwprintw(tui->heap_win, 0, 2, " Heap ");
    wattron(tui->heap_win, A_DIM);
    mvwprintw(tui->heap_win, 2, 2, "(select exploit)");
    wattroff(tui->heap_win, A_DIM);
    wrefresh(tui->heap_win);

    werase(tui->stack_win);
    box(tui->stack_win, 0, 0);
    mvwprintw(tui->stack_win, 0, 2, " Stack ");
    wattron(tui->stack_win, A_DIM);
    mvwprintw(tui->stack_win, 2, 2, "(select exploit)");
    wattroff(tui->stack_win, A_DIM);
    wrefresh(tui->stack_win);

    werase(tui->timeline_win);
    box(tui->timeline_win, 0, 0);
    mvwprintw(tui->timeline_win, 0, 2, " Timeline ");
    wattron(tui->timeline_win, A_DIM);
    mvwprintw(tui->timeline_win, 2, 2, "(select exploit to see timeline)");
    wattroff(tui->timeline_win, A_DIM);
    wrefresh(tui->timeline_win);
}

static void render_info(tui_t *tui) {
    WINDOW *win = tui->info_win;
    werase(win);
    box(win, 0, 0);

    int height, width;
    getmaxyx(win, height, width);

    bool focused = (tui->focused_panel == PANEL_INFO);
    if (focused) wattron(win, A_REVERSE);
    mvwprintw(win, 0, 2, " Event Details ");
    if (focused) wattroff(win, A_REVERSE);

    // Step counter in title bar
    wattron(win, A_DIM);
    mvwprintw(win, 0, width - 15, " Step %d/%zu ",
              tui->timeline->current_step, tui->timeline->num_entries);
    wattroff(win, A_DIM);

    const timeline_entry_t *entry = timeline_current(tui->timeline);
    if (!entry) {
        wattron(win, A_DIM);
        mvwprintw(win, height/2, (width-18)/2, "(no current event)");
        wattroff(win, A_DIM);
        wrefresh(win);
        return;
    }

    const primitive_event_t *evt = &entry->event;
    int y = 2;

    // Event type with color
    int type_color = COLOR_WHITE_PAIR;
    switch (evt->type) {
        case PRIM_ALLOC:    type_color = COLOR_GREEN_PAIR; break;
        case PRIM_FREE:     type_color = COLOR_BLUE_PAIR; break;
        case PRIM_WRITE:    type_color = COLOR_YELLOW_PAIR; break;
        case PRIM_UAF:      type_color = COLOR_RED_PAIR; break;
        case PRIM_OVERFLOW: type_color = COLOR_RED_PAIR; break;
        default: break;
    }

    wattron(win, A_BOLD | COLOR_PAIR(type_color));
    mvwprintw(win, y++, 2, "%s", primitive_type_to_string(evt->type));
    wattroff(win, A_BOLD | COLOR_PAIR(type_color));
    y++;

    // Description (prominent)
    if (evt->description) {
        wattron(win, COLOR_PAIR(COLOR_CYAN_PAIR));
        mvwprintw(win, y++, 2, "What:");
        wattroff(win, COLOR_PAIR(COLOR_CYAN_PAIR));

        // Word wrap
        const char *desc = evt->description;
        int max_w = width - 6;
        while (*desc && y < height - 8) {
            int len = strlen(desc);
            if (len > max_w) len = max_w;
            mvwprintw(win, y++, 4, "%.*s", len, desc);
            desc += len;
        }
        y++;
    }

    // Type-specific details
    wattron(win, A_DIM);
    mvwprintw(win, y++, 2, "Details:");
    wattroff(win, A_DIM);

    switch (evt->type) {
        case PRIM_ALLOC:
            mvwprintw(win, y++, 4, "ptr:  0x%04lx",
                      (unsigned long)evt->data.alloc.ptr & 0xFFFF);
            mvwprintw(win, y++, 4, "size: %zu bytes",
                      evt->data.alloc.requested_size);
            break;

        case PRIM_FREE:
            mvwprintw(win, y++, 4, "ptr:  0x%04lx",
                      (unsigned long)evt->data.free.ptr & 0xFFFF);
            if (evt->data.free.was_freed) {
                wattron(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
                mvwprintw(win, y++, 4, "DOUBLE FREE!");
                wattroff(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            }
            break;

        case PRIM_UAF:
            wattron(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            mvwprintw(win, y++, 4, "USE-AFTER-FREE!");
            wattroff(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            mvwprintw(win, y++, 4, "ptr: 0x%04lx",
                      (unsigned long)evt->data.uaf.freed_ptr & 0xFFFF);
            break;

        case PRIM_WRITE:
            mvwprintw(win, y++, 4, "dst:  0x%04lx",
                      (unsigned long)evt->data.memop.dst & 0xFFFF);
            mvwprintw(win, y++, 4, "len:  %zu", evt->data.memop.len);

            if (evt->data.memop.len > 0) {
                mvwprintw(win, y++, 4, "data:");
                wattron(win, COLOR_PAIR(COLOR_YELLOW_PAIR));
                wmove(win, y, 6);
                for (size_t i = 0; i < 8 && i < evt->data.memop.len; i++) {
                    wprintw(win, "%02x ", evt->data.memop.preview[i]);
                }
                wattroff(win, COLOR_PAIR(COLOR_YELLOW_PAIR));
                y++;

                // ASCII preview
                wattron(win, A_DIM);
                wmove(win, y, 6);
                wprintw(win, "\"");
                for (size_t i = 0; i < 12 && i < evt->data.memop.len; i++) {
                    char c = (char)evt->data.memop.preview[i];
                    if (c >= 32 && c < 127) wprintw(win, "%c", c);
                    else wprintw(win, ".");
                }
                wprintw(win, "\"");
                wattroff(win, A_DIM);
                y++;
            }
            break;

        case PRIM_OVERFLOW:
            wattron(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            mvwprintw(win, y++, 4, "BUFFER OVERFLOW!");
            wattroff(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            mvwprintw(win, y++, 4, "overflow: %zu bytes",
                      evt->data.overflow.overflow_bytes);
            break;

        default:
            break;
    }

    wrefresh(win);
}

void tui_refresh(tui_t *tui) {
    switch (tui->mode) {
        case UI_MODE_MENU:
            render_menu(tui);
            break;

        case UI_MODE_RUNNING:
        case UI_MODE_FINISHED:
            heap_view_render(tui->heap_view, tui->heap_win,
                           tui->focused_panel == PANEL_HEAP);
            stack_view_render(tui->stack_view, tui->stack_win,
                            tui->focused_panel == PANEL_STACK);
            timeline_render(tui->timeline, tui->timeline_win);
            render_info(tui);
            break;

        case UI_MODE_HELP:
            break;
    }

    render_status_bar(tui);
    refresh();
}

void tui_on_event(const primitive_event_t *evt, void *userdata) {
    tui_t *tui = userdata;
    if (!tui || !evt) return;

    timeline_add_event(tui->timeline, evt);
    heap_view_on_event(tui->heap_view, evt);
    stack_view_on_event(tui->stack_view, evt);
}

static void run_exploit_step(tui_t *tui) {
    if (tui->timeline->current_step < tui->timeline->num_entries) {
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

    heap_view_clear(tui->heap_view);
    stack_view_clear(tui->stack_view);
    timeline_clear(tui->timeline);

    event_bus_subscribe(PRIM_NONE, tui_on_event, tui);

    if (exp->setup) exp->setup(exp);
    if (exp->run) exp->run(exp);
    if (exp->cleanup) exp->cleanup(exp);

    event_bus_process();
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
                        tui->mode = UI_MODE_MENU;
                        tui->current_exploit = NULL;
                        tui_refresh(tui);
                        break;

                    case ' ':
                        run_exploit_step(tui);
                        break;

                    case 'b':
                    case 'B':
                        timeline_step_backward(tui->timeline);
                        tui_refresh(tui);
                        break;

                    case 'r':
                    case 'R':
                        while (tui->timeline->current_step < tui->timeline->num_entries) {
                            run_exploit_step(tui);
                            if (timeline_at_checkpoint(tui->timeline)) {
                                break;
                            }
                        }
                        break;

                    case '0':
                        timeline_goto_step(tui->timeline, 0);
                        heap_view_clear(tui->heap_view);
                        stack_view_clear(tui->stack_view);
                        tui_refresh(tui);
                        break;

                    case KEY_LEFT:
                    case 'h':
                    case '<':
                        tui_focus_prev(tui);
                        tui_refresh(tui);
                        break;

                    case KEY_RIGHT:
                    case 'l':
                    case '>':
                        tui_focus_next(tui);
                        tui_refresh(tui);
                        break;

                    case KEY_UP:
                    case 'k':
                        switch (tui->focused_panel) {
                            case PANEL_HEAP:
                                heap_view_scroll(tui->heap_view, -1);
                                break;
                            case PANEL_STACK:
                                stack_view_scroll(tui->stack_view, -1);
                                break;
                            default:
                                break;
                        }
                        tui_refresh(tui);
                        break;

                    case KEY_DOWN:
                    case 'j':
                        switch (tui->focused_panel) {
                            case PANEL_HEAP:
                                heap_view_scroll(tui->heap_view, 1);
                                break;
                            case PANEL_STACK:
                                stack_view_scroll(tui->stack_view, 1);
                                break;
                            default:
                                break;
                        }
                        tui_refresh(tui);
                        break;
                }
                break;

            case UI_MODE_HELP:
                if (ch == 'q' || ch == '?' || ch == 27) {
                    tui->mode = UI_MODE_MENU;
                    tui_refresh(tui);
                }
                break;
        }
    }

    return 0;
}

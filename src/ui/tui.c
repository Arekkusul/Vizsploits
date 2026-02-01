#include "tui.h"
#include "../core/event_bus.h"
#include "../core/serialize.h"
#include "../education/lesson.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>

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
    tui->timeline_filter = timeline_filter_create();
    tui->mem_diff = mem_diff_create();

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
    if (tui->timeline_filter) timeline_filter_destroy(tui->timeline_filter);
    if (tui->mem_diff) mem_diff_destroy(tui->mem_diff);

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

/* Helper: set a temporary status message */
static void set_status_msg(tui_t *tui, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(tui->status_msg, sizeof(tui->status_msg), fmt, args);
    va_end(args);
    tui->status_msg_ttl = 30; /* ~3 seconds at 100ms timeout */
}

/* Check if a type is in the breakpoint list */
static bool is_breakpoint_type(tui_t *tui, primitive_type_t type) {
    for (int i = 0; i < tui->num_breakpoints; i++) {
        if (tui->breakpoint_types[i] == type) return true;
    }
    return false;
}

/* Toggle a breakpoint type */
static void toggle_breakpoint_type(tui_t *tui, primitive_type_t type) {
    /* Check if already present */
    for (int i = 0; i < tui->num_breakpoints; i++) {
        if (tui->breakpoint_types[i] == type) {
            /* Remove by shifting */
            for (int j = i; j < tui->num_breakpoints - 1; j++) {
                tui->breakpoint_types[j] = tui->breakpoint_types[j + 1];
            }
            tui->num_breakpoints--;
            return;
        }
    }
    /* Add */
    if (tui->num_breakpoints < MAX_BREAKPOINT_TYPES) {
        tui->breakpoint_types[tui->num_breakpoints++] = type;
    }
}

/* ===== HELP SCREEN ===== */

static void render_help(tui_t *tui) {
    WINDOW *win = tui->info_win;
    werase(win);
    box(win, 0, 0);

    int height, width;
    getmaxyx(win, height, width);
    (void)width;

    wattron(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));
    mvwprintw(win, 1, 2, "VIZSPLOITS - Help");
    wattroff(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));

    int y = 3;

    wattron(win, A_BOLD);
    mvwprintw(win, y++, 2, "Navigation:");
    wattroff(win, A_BOLD);
    mvwprintw(win, y++, 4, "Space     Step forward");
    mvwprintw(win, y++, 4, "B         Step backward");
    mvwprintw(win, y++, 4, "R         Run to checkpoint/breakpoint");
    mvwprintw(win, y++, 4, "0         Reset to start");
    mvwprintw(win, y++, 4, "< / >     Switch panel focus");
    mvwprintw(win, y++, 4, "j / k     Scroll focused panel");
    y++;

    wattron(win, A_BOLD);
    mvwprintw(win, y++, 2, "Search & Filter:");
    wattroff(win, A_BOLD);
    mvwprintw(win, y++, 4, "/         Search timeline");
    mvwprintw(win, y++, 4, "f         Cycle type filter");
    mvwprintw(win, y++, 4, "F         Clear all filters");
    y++;

    wattron(win, A_BOLD);
    mvwprintw(win, y++, 2, "Tools:");
    wattroff(win, A_BOLD);
    mvwprintw(win, y++, 4, "p         Toggle breakpoint picker");
    mvwprintw(win, y++, 4, "d         Memory diff (press twice)");
    mvwprintw(win, y++, 4, "e         Toggle lesson mode");
    mvwprintw(win, y++, 4, "S         Save trace (when finished)");
    mvwprintw(win, y++, 4, "n         Single-step (ptrace mode)");
    mvwprintw(win, y++, 4, "c         Continue (ptrace mode)");
    y++;

    wattron(win, A_BOLD);
    mvwprintw(win, y++, 2, "General:");
    wattroff(win, A_BOLD);
    mvwprintw(win, y++, 4, "? / H     This help screen");
    mvwprintw(win, y++, 4, "Q         Quit / Back to menu");
    mvwprintw(win, y++, 4, "Esc       Cancel current action");
    y++;

    if (y < height - 4) {
        wattron(win, A_BOLD);
        mvwprintw(win, y++, 2, "Color Legend:");
        wattroff(win, A_BOLD);

        wattron(win, COLOR_PAIR(COLOR_GREEN_PAIR) | A_BOLD);
        mvwprintw(win, y, 4, "GREEN");
        wattroff(win, COLOR_PAIR(COLOR_GREEN_PAIR) | A_BOLD);
        mvwprintw(win, y++, 12, "= New allocation / just changed");

        wattron(win, COLOR_PAIR(COLOR_YELLOW_PAIR));
        mvwprintw(win, y, 4, "YELLOW");
        wattroff(win, COLOR_PAIR(COLOR_YELLOW_PAIR));
        mvwprintw(win, y++, 12, "= Modified / write operation");

        wattron(win, COLOR_PAIR(COLOR_RED_PAIR));
        mvwprintw(win, y, 4, "RED");
        wattroff(win, COLOR_PAIR(COLOR_RED_PAIR));
        mvwprintw(win, y++, 12, "= Corruption / vulnerability");

        wattron(win, COLOR_PAIR(COLOR_BLUE_PAIR));
        mvwprintw(win, y, 4, "BLUE");
        wattroff(win, COLOR_PAIR(COLOR_BLUE_PAIR));
        mvwprintw(win, y++, 12, "= Freed memory");

        wattron(win, COLOR_PAIR(COLOR_CYAN_PAIR));
        mvwprintw(win, y, 4, "CYAN");
        wattroff(win, COLOR_PAIR(COLOR_CYAN_PAIR));
        mvwprintw(win, y++, 12, "= Pointer / checkpoint");
    }

    wattron(win, A_DIM);
    mvwprintw(win, height - 2, 2, "Press ? or Esc to close");
    wattroff(win, A_DIM);

    wrefresh(win);

    /* Also render placeholders on other windows */
    werase(tui->heap_win);
    box(tui->heap_win, 0, 0);
    mvwprintw(tui->heap_win, 0, 2, " Heap ");
    wrefresh(tui->heap_win);

    werase(tui->stack_win);
    box(tui->stack_win, 0, 0);
    mvwprintw(tui->stack_win, 0, 2, " Stack ");
    wrefresh(tui->stack_win);

    werase(tui->timeline_win);
    box(tui->timeline_win, 0, 0);
    mvwprintw(tui->timeline_win, 0, 2, " Timeline ");
    wrefresh(tui->timeline_win);
}

/* ===== BREAKPOINT PICKER ===== */

static const struct { primitive_type_t type; const char *name; } bp_type_list[] = {
    { PRIM_ALLOC,           "ALLOC" },
    { PRIM_FREE,            "FREE" },
    { PRIM_REALLOC,         "REALLOC" },
    { PRIM_WRITE,           "WRITE" },
    { PRIM_READ,            "READ" },
    { PRIM_CALL,            "CALL" },
    { PRIM_RETURN,          "RETURN" },
    { PRIM_OVERFLOW,        "OVERFLOW" },
    { PRIM_UAF,             "UAF" },
    { PRIM_DOUBLE_FREE,     "DOUBLE-FREE" },
    { PRIM_INTEGER_OVERFLOW,"INT-OVERFLOW" },
    { PRIM_FORMAT_STRING,   "FORMAT-STRING" },
    { PRIM_CHECKPOINT,      "CHECKPOINT" },
};
static const int bp_type_count = sizeof(bp_type_list) / sizeof(bp_type_list[0]);

static void render_breakpoint_picker(tui_t *tui, int selected) {
    WINDOW *win = tui->info_win;
    werase(win);
    box(win, 0, 0);

    int height, width;
    getmaxyx(win, height, width);
    (void)width;

    wattron(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));
    mvwprintw(win, 1, 2, "Breakpoint Types");
    wattroff(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));

    wattron(win, A_DIM);
    mvwprintw(win, 2, 2, "Space=toggle  Enter=done");
    wattroff(win, A_DIM);

    int y = 4;
    for (int i = 0; i < bp_type_count && y < height - 2; i++) {
        bool active = is_breakpoint_type(tui, bp_type_list[i].type);
        bool is_sel = (i == selected);

        if (is_sel) wattron(win, A_REVERSE);

        mvwprintw(win, y, 3, " [%c] %s ",
                  active ? 'X' : ' ',
                  bp_type_list[i].name);

        if (is_sel) wattroff(win, A_REVERSE);
        y++;
    }

    wrefresh(win);
}

/* ===== STATUS BAR ===== */

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

    mvwprintw(tui->status_win, 0, 0, " [Space]=Step [R]=Run [B]=Back [?]=Help [Q]=Quit ");
    wprintw(tui->status_win, "| %s ", mode_str);

    if (tui->mode == UI_MODE_RUNNING || tui->mode == UI_MODE_FINISHED) {
        wprintw(tui->status_win, "| Focus: %s ", panel_name(tui->focused_panel));
    }

    if (tui->current_exploit) {
        wprintw(tui->status_win, "| %s ", tui->current_exploit->meta.name);
    }

    /* Show active breakpoints */
    if (tui->num_breakpoints > 0) {
        wprintw(tui->status_win, "| BP:");
        for (int i = 0; i < tui->num_breakpoints && i < 4; i++) {
            wprintw(tui->status_win, "%s ", primitive_type_short(tui->breakpoint_types[i]));
        }
        if (tui->num_breakpoints > 4) wprintw(tui->status_win, "+%d ", tui->num_breakpoints - 4);
    }

    // Fill line
    int y, x;
    getyx(tui->status_win, y, x);
    (void)y;
    for (int i = x; i < width; i++) waddch(tui->status_win, ' ');
    wattroff(tui->status_win, A_REVERSE);

    // Line 2: legend or status message or search prompt
    if (tui->search_active) {
        mvwprintw(tui->status_win, 1, 0, " Search: %s", tui->search_buf);
        wattron(tui->status_win, A_BLINK);
        waddch(tui->status_win, '_');
        wattroff(tui->status_win, A_BLINK);
    } else if (tui->status_msg_ttl > 0) {
        wattron(tui->status_win, COLOR_PAIR(COLOR_CYAN_PAIR) | A_BOLD);
        mvwprintw(tui->status_win, 1, 0, " %s", tui->status_msg);
        wattroff(tui->status_win, COLOR_PAIR(COLOR_CYAN_PAIR) | A_BOLD);
        tui->status_msg_ttl--;
    } else {
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

        /* Show filter info */
        if (tui->timeline_filter && timeline_filter_is_active(tui->timeline_filter)) {
            wprintw(tui->status_win, "| Filter: ");
            if (tui->timeline_filter->type_filter != PRIM_NONE) {
                wprintw(tui->status_win, "[%s] ",
                        primitive_type_short(tui->timeline_filter->type_filter));
            }
            if (tui->timeline_filter->search_active) {
                wprintw(tui->status_win, "\"%s\" ", tui->timeline_filter->search_text);
            }
            wprintw(tui->status_win, "(%zu matches) ", tui->timeline_filter->num_filtered);
        } else {
            wprintw(tui->status_win, "| [/]=Search [f]=Filter [p]=Breakpoints");
        }
    }

    wrefresh(tui->status_win);
}

/* ===== MENU ===== */

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
    mvwprintw(win, height - 2, 2, "[Up/Down] Select  [Enter] Run  [?] Help  [Q] Quit");
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

/* ===== FILTERED TIMELINE RENDERING ===== */

static void render_timeline_filtered(tui_t *tui) {
    timeline_t *timeline = tui->timeline;
    timeline_filter_t *filter = tui->timeline_filter;
    WINDOW *win = tui->timeline_win;

    if (!timeline || !win) return;

    int height, width;
    getmaxyx(win, height, width);

    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " Timeline ");

    /* Status line */
    wattron(win, A_DIM);
    if (filter && timeline_filter_is_active(filter)) {
        mvwprintw(win, 0, width - 28, " %zu/%zu (filtered) ",
                  filter->num_filtered, timeline->num_entries);
    } else {
        mvwprintw(win, 0, width - 20, " Step %zu/%zu ",
                  timeline->current_step, timeline->num_entries);
    }
    wattroff(win, A_DIM);

    if (timeline->num_entries == 0) {
        mvwprintw(win, 2, 2, "(no events)");
        wrefresh(win);
        return;
    }

    /* Use filtered indices if filter active, otherwise render normally */
    bool use_filter = filter && timeline_filter_is_active(filter);

    size_t total = use_filter ? filter->num_filtered : timeline->num_entries;
    int visible_lines = height - 3;
    int start = timeline->scroll_offset;

    /* Auto-scroll to keep current step visible */
    if (!use_filter) {
        if ((int)timeline->current_step < start) {
            start = (int)timeline->current_step;
        } else if ((int)timeline->current_step >= start + visible_lines) {
            start = (int)timeline->current_step - visible_lines + 1;
        }
    }
    if (start < 0) start = 0;
    timeline->scroll_offset = start;

    int y = 1;
    for (size_t vi = start; vi < total && y < height - 2; vi++) {
        size_t i = use_filter ? filter->filtered_indices[vi] : vi;
        if (i >= timeline->num_entries) continue;

        timeline_entry_t *entry = &timeline->entries[i];

        char status;
        int color_pair;

        if (i < timeline->current_step) {
            status = 'x';
            color_pair = 2; /* Green */
        } else if (i == timeline->current_step) {
            status = '>';
            color_pair = 3; /* Yellow */
        } else {
            status = ' ';
            color_pair = 0;
        }

        if (entry->is_checkpoint) wattron(win, A_BOLD);
        if (i == timeline->current_step) wattron(win, A_REVERSE);

        /* Highlight breakpoint types */
        if (is_breakpoint_type(tui, entry->event.type)) {
            color_pair = COLOR_RED_PAIR;
        }

        wattron(win, COLOR_PAIR(color_pair));

        char desc[64];
        const char *src = entry->event.description ? entry->event.description : "";
        int max_desc = width - 18;
        if (max_desc > 60) max_desc = 60;
        if (max_desc < 1) max_desc = 1;
        snprintf(desc, sizeof(desc), "%-*.*s", max_desc, max_desc, src);

        mvwprintw(win, y, 1, " %c %3zu. %-6s %s",
                  status, i + 1,
                  primitive_type_short(entry->event.type),
                  desc);

        wattroff(win, COLOR_PAIR(color_pair));
        if (i == timeline->current_step) wattroff(win, A_REVERSE);
        if (entry->is_checkpoint) wattroff(win, A_BOLD);

        y++;
    }

    mvwprintw(win, height - 1, 2, " [Space]=Step [R]=Run [B]=Back [/]=Search [f]=Filter ");

    wrefresh(win);
}

/* ===== INFO PANEL ===== */

static void render_info(tui_t *tui) {
    /* If diff mode, render diff instead */
    if (tui->diff_mode && mem_diff_ready(tui->mem_diff)) {
        mem_diff_render(tui->mem_diff, tui->info_win);
        return;
    }

    WINDOW *win = tui->info_win;
    werase(win);
    box(win, 0, 0);

    int height, width;
    getmaxyx(win, height, width);

    bool focused = (tui->focused_panel == PANEL_INFO);
    if (focused) wattron(win, A_REVERSE);
    mvwprintw(win, 0, 2, " Event Details ");
    if (focused) wattroff(win, A_REVERSE);

    wattron(win, A_DIM);
    mvwprintw(win, 0, width - 15, " Step %zu/%zu ",
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

    int type_color = COLOR_WHITE_PAIR;
    switch (evt->type) {
        case PRIM_ALLOC:    type_color = COLOR_GREEN_PAIR; break;
        case PRIM_FREE:     type_color = COLOR_BLUE_PAIR; break;
        case PRIM_WRITE:    type_color = COLOR_YELLOW_PAIR; break;
        case PRIM_UAF:      type_color = COLOR_RED_PAIR; break;
        case PRIM_OVERFLOW: type_color = COLOR_RED_PAIR; break;
        case PRIM_DOUBLE_FREE: type_color = COLOR_RED_PAIR; break;
        default: break;
    }

    wattron(win, A_BOLD | COLOR_PAIR(type_color));
    mvwprintw(win, y++, 2, "%s", primitive_type_to_string(evt->type));
    wattroff(win, A_BOLD | COLOR_PAIR(type_color));
    y++;

    if (evt->description) {
        wattron(win, COLOR_PAIR(COLOR_CYAN_PAIR));
        mvwprintw(win, y++, 2, "What:");
        wattroff(win, COLOR_PAIR(COLOR_CYAN_PAIR));

        const char *desc = evt->description;
        int max_w = width - 6;
        while (*desc && y < height - 8) {
            int len = (int)strlen(desc);
            if (len > max_w) len = max_w;
            mvwprintw(win, y++, 4, "%.*s", len, desc);
            desc += len;
        }
        y++;
    }

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

        case PRIM_DOUBLE_FREE:
            wattron(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            mvwprintw(win, y++, 4, "DOUBLE FREE!");
            wattroff(win, COLOR_PAIR(COLOR_RED_PAIR) | A_BOLD);
            mvwprintw(win, y++, 4, "ptr: 0x%04lx",
                      (unsigned long)evt->data.double_free.ptr & 0xFFFF);
            break;

        default:
            break;
    }

    /* Ptrace register display */
    if (tui->ptrace_mode && tui->ptrace_session && y < height - 6) {
        ptrace_regs_t regs;
        if (ptrace_session_get_regs(tui->ptrace_session, &regs) == 0) {
            y++;
            wattron(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));
            mvwprintw(win, y++, 2, "Registers:");
            wattroff(win, A_BOLD | COLOR_PAIR(COLOR_CYAN_PAIR));
            wattron(win, A_DIM);
            mvwprintw(win, y++, 3, "RIP %016lx  RSP %016lx", regs.rip, regs.rsp);
            mvwprintw(win, y++, 3, "RAX %016lx  RBX %016lx", regs.rax, regs.rbx);
            mvwprintw(win, y++, 3, "RCX %016lx  RDX %016lx", regs.rcx, regs.rdx);
            mvwprintw(win, y++, 3, "RDI %016lx  RSI %016lx", regs.rdi, regs.rsi);
            wattroff(win, A_DIM);
        }
    }

    /* Lesson annotations */
    if (tui->lesson_mode && tui->current_lesson && y < height - 3) {
        const lesson_t *lesson = (const lesson_t *)tui->current_lesson;
        const lesson_annotation_t *ann = NULL;

        /* Try step-based annotation first, then type-based */
        ann = lesson_get_annotation(lesson, (int)tui->timeline->current_step + 1);
        if (!ann) {
            ann = lesson_get_annotation_by_type(lesson, evt->type);
        }

        if (ann) {
            y++;
            wattron(win, COLOR_PAIR(COLOR_MAGENTA_PAIR) | A_BOLD);
            mvwprintw(win, y++, 2, "Lesson: %s", ann->title);
            wattroff(win, COLOR_PAIR(COLOR_MAGENTA_PAIR) | A_BOLD);

            /* Explanation - word wrap */
            if (ann->explanation && y < height - 2) {
                int max_w = width - 6;
                if (max_w < 10) max_w = 10;
                const char *p = ann->explanation;
                while (*p && y < height - 2) {
                    int len = (int)strlen(p);
                    if (len > max_w) len = max_w;
                    /* Try to break at a space */
                    if (len == max_w && p[len] != '\0' && p[len] != ' ') {
                        int brk = len;
                        while (brk > 0 && p[brk] != ' ') brk--;
                        if (brk > 0) len = brk;
                    }
                    mvwprintw(win, y++, 4, "%.*s", len, p);
                    p += len;
                    while (*p == ' ') p++;
                }
            }

            /* Mitigation */
            if (ann->mitigation && y < height - 1) {
                wattron(win, COLOR_PAIR(COLOR_GREEN_PAIR));
                mvwprintw(win, y++, 2, "Fix:");
                wattroff(win, COLOR_PAIR(COLOR_GREEN_PAIR));
                int max_w = width - 6;
                if (max_w < 10) max_w = 10;
                const char *p = ann->mitigation;
                while (*p && y < height - 1) {
                    int len = (int)strlen(p);
                    if (len > max_w) len = max_w;
                    if (len == max_w && p[len] != '\0' && p[len] != ' ') {
                        int brk = len;
                        while (brk > 0 && p[brk] != ' ') brk--;
                        if (brk > 0) len = brk;
                    }
                    mvwprintw(win, y++, 4, "%.*s", len, p);
                    p += len;
                    while (*p == ' ') p++;
                }
            }
        }
    }

    wrefresh(win);
}

/* ===== REFRESH ===== */

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
            render_timeline_filtered(tui);
            render_info(tui);
            break;

        case UI_MODE_HELP:
            render_help(tui);
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

        /* Re-apply filter after stepping */
        if (tui->timeline_filter && timeline_filter_is_active(tui->timeline_filter)) {
            timeline_filter_apply(tui->timeline_filter, tui->timeline);
        }

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
    if (tui->timeline_filter) timeline_filter_clear(tui->timeline_filter);
    if (tui->mem_diff) mem_diff_reset(tui->mem_diff);
    tui->diff_mode = false;

    /* Look up lesson for this exploit */
    tui->current_lesson = (void *)lesson_find(exp->meta.name);

    event_bus_subscribe(PRIM_NONE, tui_on_event, tui);

    if (exp->setup) exp->setup(exp);
    if (exp->run) exp->run(exp);
    if (exp->cleanup) exp->cleanup(exp);

    event_bus_process();
    timeline_goto_step(tui->timeline, 0);

    tui_refresh(tui);
}

/* ===== SEARCH INPUT HANDLING ===== */

static void handle_search_input(tui_t *tui, int ch) {
    if (ch == 27) { /* Escape */
        tui->search_active = false;
        tui->search_buf[0] = '\0';
        tui->search_cursor = 0;
        timeline_filter_set_search(tui->timeline_filter, "");
        timeline_filter_apply(tui->timeline_filter, tui->timeline);
    } else if (ch == '\n' || ch == KEY_ENTER) {
        /* Apply search */
        tui->search_active = false;
        timeline_filter_set_search(tui->timeline_filter, tui->search_buf);
        timeline_filter_apply(tui->timeline_filter, tui->timeline);
        set_status_msg(tui, "Search: \"%s\" (%zu matches)",
                       tui->search_buf, tui->timeline_filter->num_filtered);
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        if (tui->search_cursor > 0) {
            tui->search_buf[--tui->search_cursor] = '\0';
        }
    } else if (isprint(ch) && tui->search_cursor < (int)sizeof(tui->search_buf) - 1) {
        tui->search_buf[tui->search_cursor++] = (char)ch;
        tui->search_buf[tui->search_cursor] = '\0';
    }
}

/* ===== MAIN EVENT LOOP ===== */

int tui_run(tui_t *tui) {
    tui_refresh(tui);

    while (!tui->quit_requested) {
        int ch = getch();

        /* Search mode intercepts all input */
        if (tui->search_active) {
            handle_search_input(tui, ch);
            tui_refresh(tui);
            continue;
        }

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
                    case 'H':
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
                        tui->diff_mode = false;
                        tui_refresh(tui);
                        break;

                    case ' ':
                        if (tui->ptrace_mode && tui->ptrace_session) {
                            /* In ptrace mode, continue to next stop */
                            event_bus_process();
                            if (ptrace_session_continue(tui->ptrace_session) < 0) {
                                tui->mode = UI_MODE_FINISHED;
                            }
                            event_bus_process();
                            /* Step timeline to show new events */
                            while (tui->timeline->current_step < tui->timeline->num_entries) {
                                timeline_step_forward(tui->timeline);
                            }
                            tui_refresh(tui);
                        } else {
                            run_exploit_step(tui);
                        }
                        break;

                    case 'n':
                        /* Single-step in ptrace mode */
                        if (tui->ptrace_mode && tui->ptrace_session) {
                            event_bus_process();
                            if (ptrace_session_step(tui->ptrace_session) < 0) {
                                tui->mode = UI_MODE_FINISHED;
                            }
                            event_bus_process();
                            while (tui->timeline->current_step < tui->timeline->num_entries) {
                                timeline_step_forward(tui->timeline);
                            }
                            tui_refresh(tui);
                        }
                        break;

                    case 'c':
                        /* Continue in ptrace mode */
                        if (tui->ptrace_mode && tui->ptrace_session) {
                            event_bus_process();
                            while (ptrace_session_is_running(tui->ptrace_session)) {
                                if (ptrace_session_continue(tui->ptrace_session) < 0) {
                                    break;
                                }
                                event_bus_process();
                                while (tui->timeline->current_step < tui->timeline->num_entries) {
                                    timeline_step_forward(tui->timeline);
                                }
                                /* Check for checkpoint (breakpoint hit) */
                                if (timeline_at_checkpoint(tui->timeline)) break;
                            }
                            if (!ptrace_session_is_running(tui->ptrace_session)) {
                                tui->mode = UI_MODE_FINISHED;
                            }
                            tui_refresh(tui);
                        }
                        break;

                    case 'b':
                    case 'B':
                        timeline_step_backward(tui->timeline);
                        tui_refresh(tui);
                        break;

                    case 'r':
                    case 'R':
                        /* Run to checkpoint or breakpoint type */
                        while (tui->timeline->current_step < tui->timeline->num_entries) {
                            run_exploit_step(tui);
                            if (timeline_at_checkpoint(tui->timeline)) {
                                break;
                            }
                            /* Check breakpoints */
                            const timeline_entry_t *cur = timeline_current(tui->timeline);
                            if (cur && is_breakpoint_type(tui, cur->event.type)) {
                                set_status_msg(tui, "Hit breakpoint: %s",
                                             primitive_type_to_string(cur->event.type));
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

                    case '?':
                    case 'H':
                        tui->mode = UI_MODE_HELP;
                        tui_refresh(tui);
                        break;

                    /* Timeline search */
                    case '/':
                        tui->search_active = true;
                        tui->search_buf[0] = '\0';
                        tui->search_cursor = 0;
                        tui_refresh(tui);
                        break;

                    /* Type filter cycle */
                    case 'f':
                        if (tui->timeline_filter) {
                            timeline_filter_cycle_type(tui->timeline_filter);
                            timeline_filter_apply(tui->timeline_filter, tui->timeline);
                            if (tui->timeline_filter->type_filter == PRIM_NONE) {
                                set_status_msg(tui, "Filter: ALL types");
                            } else {
                                set_status_msg(tui, "Filter: %s (%zu matches)",
                                    primitive_type_to_string(tui->timeline_filter->type_filter),
                                    tui->timeline_filter->num_filtered);
                            }
                            tui_refresh(tui);
                        }
                        break;

                    /* Clear all filters */
                    case 'F':
                        if (tui->timeline_filter) {
                            timeline_filter_clear(tui->timeline_filter);
                            set_status_msg(tui, "Filters cleared");
                            tui_refresh(tui);
                        }
                        break;

                    /* Breakpoint picker */
                    case 'p': {
                        int bp_sel = 0;
                        bool bp_active = true;
                        while (bp_active) {
                            render_breakpoint_picker(tui, bp_sel);
                            render_status_bar(tui);
                            refresh();

                            int bch = getch();
                            switch (bch) {
                                case KEY_UP:
                                case 'k':
                                    if (bp_sel > 0) bp_sel--;
                                    break;
                                case KEY_DOWN:
                                case 'j':
                                    if (bp_sel < bp_type_count - 1) bp_sel++;
                                    break;
                                case ' ':
                                    toggle_breakpoint_type(tui, bp_type_list[bp_sel].type);
                                    break;
                                case '\n':
                                case KEY_ENTER:
                                case 27: /* Escape */
                                case 'p':
                                    bp_active = false;
                                    break;
                            }
                        }
                        set_status_msg(tui, "%d breakpoint(s) active", tui->num_breakpoints);
                        tui_refresh(tui);
                        break;
                    }

                    /* Memory diff */
                    case 'd':
                        if (tui->mem_diff) {
                            if (!mem_diff_has_a(tui->mem_diff)) {
                                /* Capture snapshot A from first heap region */
                                if (tui->heap_view->hex_view &&
                                    tui->heap_view->hex_view->num_regions > 0) {
                                    mem_diff_capture_a(tui->mem_diff,
                                                       tui->heap_view->hex_view, 0);
                                    set_status_msg(tui, "Diff: snapshot A captured. Press d again for B.");
                                } else {
                                    set_status_msg(tui, "No memory regions to diff");
                                }
                            } else if (!mem_diff_ready(tui->mem_diff)) {
                                /* Capture snapshot B */
                                if (tui->heap_view->hex_view &&
                                    tui->heap_view->hex_view->num_regions > 0) {
                                    mem_diff_capture_b(tui->mem_diff,
                                                       tui->heap_view->hex_view, 0);
                                    tui->diff_mode = true;
                                    set_status_msg(tui, "Diff: comparing snapshots. Esc to exit.");
                                }
                            } else {
                                /* Reset and start over */
                                mem_diff_reset(tui->mem_diff);
                                tui->diff_mode = false;
                                set_status_msg(tui, "Diff: reset");
                            }
                            tui_refresh(tui);
                        }
                        break;

                    case 27: /* Escape */
                        if (tui->diff_mode) {
                            tui->diff_mode = false;
                            mem_diff_reset(tui->mem_diff);
                            tui_refresh(tui);
                        }
                        break;

                    /* Save trace */
                    case 'S':
                        if (tui->timeline && tui->timeline->num_entries > 0) {
                            const char *ename = tui->current_exploit
                                ? tui->current_exploit->meta.name : "replay";
                            char out_path[256];
                            int rc = trace_save_auto(ename, tui->timeline,
                                                     out_path, sizeof(out_path));
                            if (rc == 0) {
                                set_status_msg(tui, "Trace saved: %s", out_path);
                            } else {
                                set_status_msg(tui, "Failed to save trace");
                            }
                            tui_refresh(tui);
                        } else {
                            set_status_msg(tui, "No events to save");
                            tui_refresh(tui);
                        }
                        break;

                    /* Education mode toggle */
                    case 'e':
                        tui->lesson_mode = !tui->lesson_mode;
                        if (tui->lesson_mode && tui->current_lesson) {
                            const lesson_t *les = (const lesson_t *)tui->current_lesson;
                            set_status_msg(tui, "Lesson: %s", les->exploit_name);
                        } else if (tui->lesson_mode) {
                            set_status_msg(tui, "Lesson mode ON (no lesson for this exploit)");
                        } else {
                            set_status_msg(tui, "Lesson mode OFF");
                        }
                        tui_refresh(tui);
                        break;
                }
                break;

            case UI_MODE_HELP:
                if (ch == 'q' || ch == '?' || ch == 27 || ch == 'H') {
                    tui->mode = UI_MODE_MENU;
                    /* Return to previous mode if we came from running */
                    if (tui->current_exploit) {
                        tui->mode = tui->timeline->current_step >= tui->timeline->num_entries
                            ? UI_MODE_FINISHED : UI_MODE_RUNNING;
                    }
                    tui_refresh(tui);
                }
                break;
        }
    }

    return 0;
}

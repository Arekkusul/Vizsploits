#ifndef TUI_H
#define TUI_H

#include "../visualization/heap_view.h"
#include "../visualization/stack_view.h"
#include "../visualization/timeline.h"
#include "../visualization/timeline_filter.h"
#include "../visualization/mem_diff.h"
#include "../exploits/api/exploit_api.h"
#include "../instrumentation/ptrace/ptrace_backend.h"
#include <ncurses.h>

/**
 * UI Mode
 */
typedef enum {
    UI_MODE_MENU,       // Exploit selection menu
    UI_MODE_RUNNING,    // Exploit running (step-by-step)
    UI_MODE_FINISHED,   // Exploit completed
    UI_MODE_HELP        // Help screen
} ui_mode_t;

/**
 * Panel focus
 */
typedef enum {
    PANEL_HEAP,
    PANEL_STACK,
    PANEL_INFO,
    PANEL_TIMELINE,
    PANEL_COUNT
} panel_focus_t;

/* Maximum breakpoint types */
#define MAX_BREAKPOINT_TYPES 24

/**
 * TUI State
 */
typedef struct {
    // Windows
    WINDOW *main_win;
    WINDOW *heap_win;
    WINDOW *stack_win;
    WINDOW *timeline_win;
    WINDOW *info_win;
    WINDOW *status_win;

    // Views
    heap_view_t *heap_view;
    stack_view_t *stack_view;
    timeline_t *timeline;

    // Timeline filter/search
    timeline_filter_t *timeline_filter;
    bool search_active;
    char search_buf[128];
    int search_cursor;

    // Breakpoints on event types
    primitive_type_t breakpoint_types[MAX_BREAKPOINT_TYPES];
    int num_breakpoints;

    // Memory diff
    mem_diff_t *mem_diff;
    bool diff_mode;

    // State
    ui_mode_t mode;
    int selected_exploit;
    exploit_t *current_exploit;
    bool running;
    bool step_mode;
    bool quit_requested;

    // Panel focus
    panel_focus_t focused_panel;

    // Status message (temporary)
    char status_msg[128];
    int status_msg_ttl;  // frames to display

    // Education/lesson mode (Feature 4)
    void *current_lesson;  // lesson_t* (forward declared)
    bool lesson_mode;

    // Ptrace mode (Feature 2)
    ptrace_session_t *ptrace_session;
    bool ptrace_mode;

    // Dimensions
    int term_height;
    int term_width;
} tui_t;

/**
 * Initialize TUI
 */
tui_t *tui_init(void);

/**
 * Cleanup TUI
 */
void tui_cleanup(tui_t *tui);

/**
 * Main event loop
 */
int tui_run(tui_t *tui);

/**
 * Handle resize
 */
void tui_handle_resize(tui_t *tui);

/**
 * Refresh all windows
 */
void tui_refresh(tui_t *tui);

/**
 * Show menu
 */
void tui_show_menu(tui_t *tui);

/**
 * Handle event from exploit
 */
void tui_on_event(const primitive_event_t *evt, void *userdata);

/**
 * Switch panel focus
 */
void tui_focus_next(tui_t *tui);
void tui_focus_prev(tui_t *tui);

#endif // TUI_H

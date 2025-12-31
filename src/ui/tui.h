#ifndef TUI_H
#define TUI_H

#include "../visualization/heap_view.h"
#include "../visualization/timeline.h"
#include "../exploits/api/exploit_api.h"
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
 * TUI State
 */
typedef struct {
    // Windows
    WINDOW *main_win;
    WINDOW *heap_win;
    WINDOW *timeline_win;
    WINDOW *info_win;
    WINDOW *status_win;

    // Views
    heap_view_t *heap_view;
    timeline_t *timeline;

    // State
    ui_mode_t mode;
    int selected_exploit;
    exploit_t *current_exploit;
    bool running;
    bool step_mode;
    bool quit_requested;

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

#endif // TUI_H

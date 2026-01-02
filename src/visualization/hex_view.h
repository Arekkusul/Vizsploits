#ifndef HEX_VIEW_H
#define HEX_VIEW_H

#include <ncurses.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Maximum bytes to store per memory region
 */
#define HEX_MAX_REGION_SIZE 4096
#define HEX_MAX_REGIONS 64

/**
 * Byte state for highlighting
 */
typedef enum {
    BYTE_NORMAL,
    BYTE_MODIFIED,      // Recently written
    BYTE_JUST_CHANGED,  // Just changed this step (flash highlight)
    BYTE_CORRUPTED,     // Overflow/corruption
    BYTE_FREED,         // Part of freed memory
    BYTE_POINTER,       // Known pointer value
    BYTE_SHELLCODE      // Executable code
} byte_state_t;

/**
 * Memory region for hex view
 */
typedef struct {
    void *base_addr;                    // Original address (for display)
    uint8_t data[HEX_MAX_REGION_SIZE];  // Actual data
    byte_state_t state[HEX_MAX_REGION_SIZE]; // Per-byte state
    size_t size;                        // Current size
    size_t capacity;                    // Max size
    const char *label;                  // Region label
    bool is_valid;                      // Is region active
    bool is_freed;                      // Was this region freed
    int highlight_start;                // Highlight range start (-1 = none)
    int highlight_end;                  // Highlight range end
} hex_region_t;

/**
 * Hex view state
 */
typedef struct {
    hex_region_t regions[HEX_MAX_REGIONS];
    size_t num_regions;
    int scroll_offset;          // Scroll offset in lines
    int bytes_per_row;          // Bytes per row (8 or 16)
    bool show_ascii;            // Show ASCII column
    int selected_region;        // Currently selected region (-1 = none)
    int cursor_offset;          // Cursor position within region
} hex_view_t;

/**
 * Create hex view
 */
hex_view_t *hex_view_create(void);

/**
 * Destroy hex view
 */
void hex_view_destroy(hex_view_t *view);

/**
 * Add or update a memory region
 * Returns region index, or -1 on failure
 */
int hex_view_add_region(hex_view_t *view, void *addr, size_t size, const char *label);

/**
 * Update region data (e.g., after a write)
 */
void hex_view_update_region(hex_view_t *view, int region_idx,
                            size_t offset, const uint8_t *data, size_t len,
                            byte_state_t state);

/**
 * Set byte states for a range
 */
void hex_view_set_state(hex_view_t *view, int region_idx,
                        size_t offset, size_t len, byte_state_t state);

/**
 * Mark region as freed
 */
void hex_view_mark_freed(hex_view_t *view, int region_idx);

/**
 * Remove a region
 */
void hex_view_remove_region(hex_view_t *view, int region_idx);

/**
 * Find region by address
 */
int hex_view_find_region(hex_view_t *view, void *addr);

/**
 * Render hex view to ncurses window
 */
void hex_view_render(hex_view_t *view, WINDOW *win, bool focused);

/**
 * Render a single region's hex dump
 */
void hex_view_render_region(hex_view_t *view, WINDOW *win,
                            int region_idx, int start_y, int max_lines,
                            bool show_header);

/**
 * Scroll the view
 */
void hex_view_scroll(hex_view_t *view, int delta);

/**
 * Clear all regions
 */
void hex_view_clear(hex_view_t *view);

/**
 * Set highlight range for a region
 */
void hex_view_set_highlight(hex_view_t *view, int region_idx,
                            int start, int end);

/**
 * Decay "just changed" states to "modified" (call after each step)
 */
void hex_view_decay_highlights(hex_view_t *view);

/**
 * Utility: Render a hex dump line (compact format)
 * Returns number of characters written
 */
int hex_render_line(WINDOW *win, int y, int x,
                    void *display_addr, const uint8_t *data,
                    const byte_state_t *states, size_t len,
                    bool show_ascii, int bytes_per_row);

/**
 * Utility: Render a hex dump line with short address
 */
int hex_render_line_short(WINDOW *win, int y, int x,
                          uint32_t addr_low, const uint8_t *data,
                          const byte_state_t *states, size_t len,
                          bool show_ascii, int bytes_per_row);

#endif // HEX_VIEW_H

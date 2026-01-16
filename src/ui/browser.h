#ifndef BROWSER_H
#define BROWSER_H

#include "core/filesystem.h"
#include <stdbool.h>

// Forward declaration
struct App;

// Double-click and hover debounce timing
#define DOUBLE_CLICK_TIME 0.5    // seconds
#define HOVER_DEBOUNCE_TIME 0.5  // seconds before triggering AI summary

// Hover summary state for async AI summarization
typedef enum HoverSummaryState {
    HOVER_IDLE,       // Not hovering or no summary needed
    HOVER_DEBOUNCING, // Waiting for debounce timer
    HOVER_LOADING,    // API call in progress
    HOVER_READY,      // Summary available
    HOVER_ERROR       // Error occurred
} HoverSummaryState;

// Browser input/hover state
typedef struct BrowserState {
    // Hover tracking
    int hovered_index;           // -1 if none
    double hover_start_time;     // When hover began (for debounce)

    // Double-click detection
    int last_click_index;        // Index of last clicked item
    double last_click_time;      // Time of last click

    // AI Summary hover state
    HoverSummaryState summary_state;
    char summary_text[4096];     // Cached summary text
    char summary_path[PATH_MAX_LEN];  // Path being/was summarized
    char summary_error[256];     // Error message if any
} BrowserState;

// Draw the file browser list
void browser_draw(struct App *app);

// Handle browser input (mouse for rubber band selection, etc.)
void browser_handle_input(struct App *app);

// Draw rubber band selection overlay (call after browser_draw)
void browser_draw_rubber_band(struct App *app);

// Ensure the selected item is visible (adjust scroll)
void browser_ensure_visible(struct App *app);

#endif // BROWSER_H

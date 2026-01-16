#ifndef DUAL_PANE_H
#define DUAL_PANE_H

#include "../core/filesystem.h"
#include <stdbool.h>

// Forward declaration
struct App;

// Pane identifiers
typedef enum {
    PANE_LEFT = 0,
    PANE_RIGHT = 1
} PaneId;

// Pane state - represents one side of the dual pane view
typedef struct PaneState {
    DirectoryState directory;     // Directory contents for this pane
    int selected_index;           // Selected item in this pane
    int scroll_offset;            // Scroll position
    char current_path[PATH_MAX_LEN];
} PaneState;

// Directory comparison result for a single file
typedef enum {
    COMPARE_SAME,                 // File exists in both with same content
    COMPARE_DIFFERENT,            // File exists in both but different
    COMPARE_LEFT_ONLY,            // File only exists in left pane
    COMPARE_RIGHT_ONLY,           // File only exists in right pane
    COMPARE_DIR                   // Item is a directory (not compared)
} CompareResult;

// Dual pane mode state
typedef struct DualPaneState {
    bool enabled;                 // Whether dual pane mode is active
    PaneState left;               // Left pane state
    PaneState right;              // Right pane state
    PaneId active_pane;           // Which pane has focus
    bool sync_scroll;             // Whether to sync scroll between panes
    bool compare_mode;            // Whether to show comparison view
    CompareResult *compare_results_left;  // Comparison results for left pane
    CompareResult *compare_results_right; // Comparison results for right pane
    int compare_count_left;
    int compare_count_right;
} DualPaneState;

// Initialize dual pane state
void dual_pane_init(DualPaneState *state);

// Free dual pane resources
void dual_pane_free(DualPaneState *state);

// Enable/disable dual pane mode
void dual_pane_toggle(struct App *app);
bool dual_pane_is_enabled(DualPaneState *state);

// Switch active pane
void dual_pane_switch_pane(struct App *app);

// Get active pane
PaneId dual_pane_get_active(DualPaneState *state);
PaneState* dual_pane_get_active_pane(DualPaneState *state);
PaneState* dual_pane_get_inactive_pane(DualPaneState *state);

// Navigate in active pane
bool dual_pane_navigate_to(struct App *app, const char *path);
bool dual_pane_enter_directory(struct App *app);
bool dual_pane_go_parent(struct App *app);

// Transfer files between panes
void dual_pane_copy_to_other(struct App *app);
void dual_pane_move_to_other(struct App *app);

// Synchronized scrolling
void dual_pane_toggle_sync_scroll(DualPaneState *state);

// Directory comparison
void dual_pane_toggle_compare(struct App *app);
void dual_pane_run_comparison(struct App *app);
CompareResult dual_pane_get_compare_result(DualPaneState *state, PaneId pane, int index);

// Handle input for dual pane mode
void dual_pane_handle_input(struct App *app);

// Draw dual pane view
void dual_pane_draw(struct App *app);

// Sync state between dual pane and main app
void dual_pane_sync_to_app(struct App *app);
void dual_pane_sync_from_app(struct App *app);

#endif // DUAL_PANE_H

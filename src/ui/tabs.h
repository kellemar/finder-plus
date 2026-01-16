#ifndef TABS_H
#define TABS_H

#include "../core/filesystem.h"
#include <stdbool.h>

#define MAX_TABS 32
#define TAB_HEIGHT 28
#define TAB_MIN_WIDTH 100
#define TAB_MAX_WIDTH 200
#define TAB_CLOSE_SIZE 16

// Single tab state
typedef struct Tab {
    char path[PATH_MAX_LEN];          // Current directory path
    char title[64];                    // Display title (folder name)
    int selected_index;                // Selected file in this tab
    int scroll_offset;                 // Scroll position
    bool active;                       // Whether this tab slot is in use
} Tab;

// Tab manager state
typedef struct TabState {
    Tab tabs[MAX_TABS];
    int count;                         // Number of active tabs
    int current;                       // Index of current tab
    int hovered;                       // Index of hovered tab (-1 if none)
    bool close_hovered;                // Whether close button is hovered
    bool dragging;                     // Whether dragging a tab
    int drag_index;                    // Index of tab being dragged
    int drag_offset_x;                 // X offset during drag
} TabState;

// Forward declaration
struct App;

// Initialize tab state
void tabs_init(TabState *tabs);

// Create a new tab with the given path
int tabs_new(TabState *tabs, const char *path);

// Close a tab by index
void tabs_close(TabState *tabs, int index);

// Switch to a tab by index
void tabs_switch(TabState *tabs, int index);

// Switch to next tab
void tabs_next(TabState *tabs);

// Switch to previous tab
void tabs_prev(TabState *tabs);

// Update tab title from path
void tabs_update_title(Tab *tab);

// Sync current tab with app state
void tabs_sync_from_app(TabState *tabs, struct App *app);

// Restore app state from current tab
void tabs_sync_to_app(TabState *tabs, struct App *app);

// Handle tab input (clicks, shortcuts)
void tabs_handle_input(struct App *app);

// Draw tab bar
void tabs_draw(struct App *app);

// Get tab bar height
int tabs_get_height(TabState *tabs);

#endif // TABS_H

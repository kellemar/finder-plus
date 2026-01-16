#ifndef CONTEXT_MENU_H
#define CONTEXT_MENU_H

#include <stdbool.h>

// Forward declaration
struct App;

// Maximum menu items
#define CONTEXT_MENU_MAX_ITEMS 16

// Context type determines which items appear
typedef enum ContextMenuType {
    CONTEXT_NONE,
    CONTEXT_FILE,
    CONTEXT_FOLDER,
    CONTEXT_EMPTY_SPACE,
    CONTEXT_MULTI_SELECT
} ContextMenuType;

// Menu item action callback
typedef void (*ContextMenuAction)(struct App *app);

// Menu item definition
typedef struct ContextMenuItem {
    char label[64];
    char shortcut[16];
    bool enabled;
    bool separator_after;
    ContextMenuAction action;
} ContextMenuItem;

// Context menu state
typedef struct ContextMenuState {
    bool visible;
    ContextMenuType type;

    // Position (where right-click occurred)
    int x;
    int y;

    // Target file/folder info
    int target_index;
    char target_path[4096];

    // Menu items (populated based on context)
    ContextMenuItem items[CONTEXT_MENU_MAX_ITEMS];
    int item_count;

    // Interaction state
    int hovered_index;
    int selected_index;
} ContextMenuState;

// Initialize context menu state
void context_menu_init(ContextMenuState *menu);

// Show context menu at position for given context
void context_menu_show(ContextMenuState *menu, struct App *app,
                       int x, int y, int target_index);

// Hide the context menu
void context_menu_hide(ContextMenuState *menu);

// Check if menu is visible
bool context_menu_is_visible(ContextMenuState *menu);

// Handle input - returns true if input was consumed
bool context_menu_handle_input(struct App *app);

// Draw the context menu
void context_menu_draw(struct App *app);

#endif // CONTEXT_MENU_H

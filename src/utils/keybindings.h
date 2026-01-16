#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

#include <stdbool.h>

// Maximum number of custom keybindings
#define MAX_KEYBINDINGS 128

// Modifier flags
typedef enum KeyModifier {
    MOD_NONE  = 0,
    MOD_CTRL  = 1 << 0,
    MOD_SHIFT = 1 << 1,
    MOD_ALT   = 1 << 2,
    MOD_SUPER = 1 << 3  // Cmd on macOS
} KeyModifier;

// Action identifiers matching palette commands
typedef enum KeyAction {
    ACTION_NONE = 0,
    // File operations
    ACTION_NEW_FILE,
    ACTION_NEW_FOLDER,
    ACTION_OPEN,
    ACTION_COPY,
    ACTION_CUT,
    ACTION_PASTE,
    ACTION_DELETE,
    ACTION_RENAME,
    ACTION_DUPLICATE,
    ACTION_SELECT_ALL,
    // View
    ACTION_VIEW_LIST,
    ACTION_VIEW_GRID,
    ACTION_VIEW_COLUMNS,
    ACTION_TOGGLE_HIDDEN,
    ACTION_TOGGLE_PREVIEW,
    ACTION_TOGGLE_SIDEBAR,
    ACTION_TOGGLE_FULLSCREEN,
    ACTION_TOGGLE_THEME,
    // Navigation
    ACTION_GO_BACK,
    ACTION_GO_FORWARD,
    ACTION_GO_PARENT,
    ACTION_GO_HOME,
    ACTION_REFRESH,
    // Tabs
    ACTION_NEW_TAB,
    ACTION_CLOSE_TAB,
    ACTION_NEXT_TAB,
    ACTION_PREV_TAB,
    // Special
    ACTION_COMMAND_PALETTE,
    ACTION_AI_COMMAND,
    ACTION_SHOW_QUEUE,
    ACTION_QUIT,
    ACTION_COUNT
} KeyAction;

// Single keybinding
typedef struct KeyBinding {
    KeyAction action;
    int key;           // raylib KEY_* constant
    int modifiers;     // Combination of KeyModifier flags
    bool is_default;   // Whether this is a default binding (vs user-set)
} KeyBinding;

// Keybinding configuration
typedef struct KeyBindingConfig {
    KeyBinding bindings[MAX_KEYBINDINGS];
    int count;
    bool modified;     // Whether config has unsaved changes
} KeyBindingConfig;

// Initialize keybindings with defaults
void keybindings_init(KeyBindingConfig *config);

// Free keybinding resources
void keybindings_free(KeyBindingConfig *config);

// Load keybindings from config file (merges with defaults)
bool keybindings_load(KeyBindingConfig *config, const char *path);

// Save keybindings to config file (only non-default bindings)
bool keybindings_save(const KeyBindingConfig *config, const char *path);

// Reset all keybindings to defaults
void keybindings_reset_defaults(KeyBindingConfig *config);

// Set a keybinding for an action
bool keybindings_set(KeyBindingConfig *config, KeyAction action, int key, int modifiers);

// Remove a keybinding for an action
bool keybindings_remove(KeyBindingConfig *config, KeyAction action);

// Get keybinding for an action (returns NULL if not bound)
const KeyBinding *keybindings_get(const KeyBindingConfig *config, KeyAction action);

// Check if a key+modifier combo is pressed and matches an action
KeyAction keybindings_check_pressed(const KeyBindingConfig *config);

// Check for conflicts (returns action that conflicts, or ACTION_NONE)
KeyAction keybindings_find_conflict(const KeyBindingConfig *config, int key, int modifiers, KeyAction exclude);

// Get action name string
const char *keybindings_action_name(KeyAction action);

// Get shortcut string (e.g., "Cmd+C")
const char *keybindings_shortcut_string(const KeyBinding *binding);

// Parse a shortcut string (e.g., "Cmd+Shift+N") into key and modifiers
bool keybindings_parse_shortcut(const char *shortcut, int *out_key, int *out_modifiers);

// Get current modifiers pressed
int keybindings_get_current_modifiers(void);

#endif // KEYBINDINGS_H

#include "keybindings.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Static buffer for shortcut strings
static char shortcut_buffer[64];

// Action name lookup table
static const char *action_names[] = {
    [ACTION_NONE] = "none",
    [ACTION_NEW_FILE] = "new_file",
    [ACTION_NEW_FOLDER] = "new_folder",
    [ACTION_OPEN] = "open",
    [ACTION_COPY] = "copy",
    [ACTION_CUT] = "cut",
    [ACTION_PASTE] = "paste",
    [ACTION_DELETE] = "delete",
    [ACTION_RENAME] = "rename",
    [ACTION_DUPLICATE] = "duplicate",
    [ACTION_SELECT_ALL] = "select_all",
    [ACTION_VIEW_LIST] = "view_list",
    [ACTION_VIEW_GRID] = "view_grid",
    [ACTION_VIEW_COLUMNS] = "view_columns",
    [ACTION_TOGGLE_HIDDEN] = "toggle_hidden",
    [ACTION_TOGGLE_PREVIEW] = "toggle_preview",
    [ACTION_TOGGLE_SIDEBAR] = "toggle_sidebar",
    [ACTION_TOGGLE_FULLSCREEN] = "toggle_fullscreen",
    [ACTION_TOGGLE_THEME] = "toggle_theme",
    [ACTION_GO_BACK] = "go_back",
    [ACTION_GO_FORWARD] = "go_forward",
    [ACTION_GO_PARENT] = "go_parent",
    [ACTION_GO_HOME] = "go_home",
    [ACTION_REFRESH] = "refresh",
    [ACTION_NEW_TAB] = "new_tab",
    [ACTION_CLOSE_TAB] = "close_tab",
    [ACTION_NEXT_TAB] = "next_tab",
    [ACTION_PREV_TAB] = "prev_tab",
    [ACTION_COMMAND_PALETTE] = "command_palette",
    [ACTION_AI_COMMAND] = "ai_command",
    [ACTION_SHOW_QUEUE] = "show_queue",
    [ACTION_QUIT] = "quit"
};

// Default keybindings
static const struct {
    KeyAction action;
    int key;
    int modifiers;
} default_bindings[] = {
    // File operations (Cmd+key on macOS)
    { ACTION_NEW_FILE,    KEY_N, MOD_SUPER | MOD_SHIFT },
    { ACTION_NEW_FOLDER,  KEY_N, MOD_SUPER | MOD_ALT },
    { ACTION_OPEN,        KEY_O, MOD_SUPER },
    { ACTION_COPY,        KEY_C, MOD_SUPER },
    { ACTION_CUT,         KEY_X, MOD_SUPER },
    { ACTION_PASTE,       KEY_V, MOD_SUPER },
    { ACTION_DELETE,      KEY_BACKSPACE, MOD_SUPER },
    { ACTION_RENAME,      KEY_ENTER, MOD_NONE },
    { ACTION_DUPLICATE,   KEY_D, MOD_SUPER },
    { ACTION_SELECT_ALL,  KEY_A, MOD_SUPER },
    // View
    { ACTION_VIEW_LIST,      KEY_ONE,   MOD_SUPER },
    { ACTION_VIEW_GRID,      KEY_TWO,   MOD_SUPER },
    { ACTION_VIEW_COLUMNS,   KEY_THREE, MOD_SUPER },
    { ACTION_TOGGLE_HIDDEN,  KEY_PERIOD, MOD_SUPER | MOD_SHIFT },
    { ACTION_TOGGLE_PREVIEW, KEY_P, MOD_SUPER | MOD_SHIFT },
    { ACTION_TOGGLE_SIDEBAR, KEY_S, MOD_SUPER | MOD_SHIFT },
    { ACTION_TOGGLE_FULLSCREEN, KEY_F, MOD_SUPER | MOD_CTRL },
    { ACTION_TOGGLE_THEME,   KEY_T, MOD_SUPER | MOD_SHIFT },
    // Navigation
    { ACTION_GO_BACK,    KEY_LEFT_BRACKET,  MOD_SUPER },
    { ACTION_GO_FORWARD, KEY_RIGHT_BRACKET, MOD_SUPER },
    { ACTION_GO_PARENT,  KEY_UP,    MOD_SUPER },
    { ACTION_GO_HOME,    KEY_H,     MOD_SUPER | MOD_SHIFT },
    { ACTION_REFRESH,    KEY_R,     MOD_SUPER },
    // Tabs
    { ACTION_NEW_TAB,    KEY_T, MOD_SUPER },
    { ACTION_CLOSE_TAB,  KEY_W, MOD_SUPER },
    { ACTION_NEXT_TAB,   KEY_TAB, MOD_CTRL },
    { ACTION_PREV_TAB,   KEY_TAB, MOD_CTRL | MOD_SHIFT },
    // Special
    { ACTION_COMMAND_PALETTE, KEY_P, MOD_SUPER | MOD_SHIFT },
    { ACTION_AI_COMMAND,      KEY_K, MOD_SUPER },
    { ACTION_SHOW_QUEUE,      KEY_Q, MOD_SUPER | MOD_SHIFT },
    { ACTION_QUIT,            KEY_Q, MOD_SUPER }
};

static const int default_bindings_count = sizeof(default_bindings) / sizeof(default_bindings[0]);

void keybindings_init(KeyBindingConfig *config)
{
    memset(config, 0, sizeof(KeyBindingConfig));
    keybindings_reset_defaults(config);
}

void keybindings_free(KeyBindingConfig *config)
{
    // Nothing to free currently
    (void)config;
}

void keybindings_reset_defaults(KeyBindingConfig *config)
{
    config->count = 0;
    config->modified = false;

    for (int i = 0; i < default_bindings_count; i++) {
        KeyBinding binding = {
            .action = default_bindings[i].action,
            .key = default_bindings[i].key,
            .modifiers = default_bindings[i].modifiers,
            .is_default = true
        };
        config->bindings[config->count++] = binding;
    }
}

bool keybindings_load(KeyBindingConfig *config, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Skip comments and empty lines
        char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        // Parse: action_name = shortcut
        char action_name[64];
        char shortcut[64];

        if (sscanf(p, "%63[^=]=%63[^\n]", action_name, shortcut) != 2) {
            continue;
        }

        // Trim whitespace
        char *name_end = action_name + strlen(action_name) - 1;
        while (name_end > action_name && isspace(*name_end)) *name_end-- = '\0';
        char *name_start = action_name;
        while (*name_start && isspace(*name_start)) name_start++;

        char *shortcut_start = shortcut;
        while (*shortcut_start && isspace(*shortcut_start)) shortcut_start++;
        char *shortcut_end = shortcut_start + strlen(shortcut_start) - 1;
        while (shortcut_end > shortcut_start && isspace(*shortcut_end)) *shortcut_end-- = '\0';

        // Find action by name
        KeyAction action = ACTION_NONE;
        for (int i = 1; i < ACTION_COUNT; i++) {
            if (strcmp(action_names[i], name_start) == 0) {
                action = (KeyAction)i;
                break;
            }
        }

        if (action == ACTION_NONE) continue;

        // Parse shortcut
        int key, modifiers;
        if (!keybindings_parse_shortcut(shortcut_start, &key, &modifiers)) {
            continue;
        }

        // Set the binding (overrides default)
        keybindings_set(config, action, key, modifiers);
    }

    fclose(file);
    config->modified = false;
    return true;
}

bool keybindings_save(const KeyBindingConfig *config, const char *path)
{
    FILE *file = fopen(path, "w");
    if (!file) {
        return false;
    }

    fprintf(file, "# Finder Plus Keybindings\n");
    fprintf(file, "# Format: action_name = shortcut\n");
    fprintf(file, "# Modifiers: Cmd, Ctrl, Shift, Alt\n");
    fprintf(file, "# Example: copy = Cmd+C\n\n");

    for (int i = 0; i < config->count; i++) {
        const KeyBinding *binding = &config->bindings[i];

        // Only save non-default bindings
        if (binding->is_default) continue;

        const char *name = keybindings_action_name(binding->action);
        const char *shortcut = keybindings_shortcut_string(binding);

        fprintf(file, "%s = %s\n", name, shortcut);
    }

    fclose(file);
    return true;
}

bool keybindings_set(KeyBindingConfig *config, KeyAction action, int key, int modifiers)
{
    if (action <= ACTION_NONE || action >= ACTION_COUNT) {
        return false;
    }

    // Check if action already has a binding
    for (int i = 0; i < config->count; i++) {
        if (config->bindings[i].action == action) {
            config->bindings[i].key = key;
            config->bindings[i].modifiers = modifiers;
            config->bindings[i].is_default = false;
            config->modified = true;
            return true;
        }
    }

    // Add new binding
    if (config->count >= MAX_KEYBINDINGS) {
        return false;
    }

    KeyBinding binding = {
        .action = action,
        .key = key,
        .modifiers = modifiers,
        .is_default = false
    };
    config->bindings[config->count++] = binding;
    config->modified = true;
    return true;
}

bool keybindings_remove(KeyBindingConfig *config, KeyAction action)
{
    for (int i = 0; i < config->count; i++) {
        if (config->bindings[i].action == action) {
            // Shift remaining bindings
            for (int j = i; j < config->count - 1; j++) {
                config->bindings[j] = config->bindings[j + 1];
            }
            config->count--;
            config->modified = true;
            return true;
        }
    }
    return false;
}

const KeyBinding *keybindings_get(const KeyBindingConfig *config, KeyAction action)
{
    for (int i = 0; i < config->count; i++) {
        if (config->bindings[i].action == action) {
            return &config->bindings[i];
        }
    }
    return NULL;
}

int keybindings_get_current_modifiers(void)
{
    int mods = MOD_NONE;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        mods |= MOD_CTRL;
    }
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
        mods |= MOD_SHIFT;
    }
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) {
        mods |= MOD_ALT;
    }
    if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) {
        mods |= MOD_SUPER;
    }
    return mods;
}

KeyAction keybindings_check_pressed(const KeyBindingConfig *config)
{
    int current_mods = keybindings_get_current_modifiers();

    for (int i = 0; i < config->count; i++) {
        const KeyBinding *binding = &config->bindings[i];

        // Check if key is pressed and modifiers match exactly
        if (IsKeyPressed(binding->key) && binding->modifiers == current_mods) {
            return binding->action;
        }
    }

    return ACTION_NONE;
}

KeyAction keybindings_find_conflict(const KeyBindingConfig *config, int key, int modifiers, KeyAction exclude)
{
    for (int i = 0; i < config->count; i++) {
        const KeyBinding *binding = &config->bindings[i];
        if (binding->action != exclude &&
            binding->key == key &&
            binding->modifiers == modifiers) {
            return binding->action;
        }
    }
    return ACTION_NONE;
}

const char *keybindings_action_name(KeyAction action)
{
    if (action < 0 || action >= ACTION_COUNT) {
        return "unknown";
    }
    return action_names[action];
}

// Key name lookup
static const char *key_name(int key)
{
    switch (key) {
        case KEY_A: return "A";
        case KEY_B: return "B";
        case KEY_C: return "C";
        case KEY_D: return "D";
        case KEY_E: return "E";
        case KEY_F: return "F";
        case KEY_G: return "G";
        case KEY_H: return "H";
        case KEY_I: return "I";
        case KEY_J: return "J";
        case KEY_K: return "K";
        case KEY_L: return "L";
        case KEY_M: return "M";
        case KEY_N: return "N";
        case KEY_O: return "O";
        case KEY_P: return "P";
        case KEY_Q: return "Q";
        case KEY_R: return "R";
        case KEY_S: return "S";
        case KEY_T: return "T";
        case KEY_U: return "U";
        case KEY_V: return "V";
        case KEY_W: return "W";
        case KEY_X: return "X";
        case KEY_Y: return "Y";
        case KEY_Z: return "Z";
        case KEY_ZERO: return "0";
        case KEY_ONE: return "1";
        case KEY_TWO: return "2";
        case KEY_THREE: return "3";
        case KEY_FOUR: return "4";
        case KEY_FIVE: return "5";
        case KEY_SIX: return "6";
        case KEY_SEVEN: return "7";
        case KEY_EIGHT: return "8";
        case KEY_NINE: return "9";
        case KEY_SPACE: return "Space";
        case KEY_ENTER: return "Enter";
        case KEY_BACKSPACE: return "Backspace";
        case KEY_TAB: return "Tab";
        case KEY_ESCAPE: return "Escape";
        case KEY_UP: return "Up";
        case KEY_DOWN: return "Down";
        case KEY_LEFT: return "Left";
        case KEY_RIGHT: return "Right";
        case KEY_HOME: return "Home";
        case KEY_END: return "End";
        case KEY_PAGE_UP: return "PageUp";
        case KEY_PAGE_DOWN: return "PageDown";
        case KEY_INSERT: return "Insert";
        case KEY_DELETE: return "Delete";
        case KEY_F1: return "F1";
        case KEY_F2: return "F2";
        case KEY_F3: return "F3";
        case KEY_F4: return "F4";
        case KEY_F5: return "F5";
        case KEY_F6: return "F6";
        case KEY_F7: return "F7";
        case KEY_F8: return "F8";
        case KEY_F9: return "F9";
        case KEY_F10: return "F10";
        case KEY_F11: return "F11";
        case KEY_F12: return "F12";
        case KEY_LEFT_BRACKET: return "[";
        case KEY_RIGHT_BRACKET: return "]";
        case KEY_MINUS: return "-";
        case KEY_EQUAL: return "=";
        case KEY_COMMA: return ",";
        case KEY_PERIOD: return ".";
        case KEY_SLASH: return "/";
        case KEY_BACKSLASH: return "\\";
        case KEY_SEMICOLON: return ";";
        case KEY_APOSTROPHE: return "'";
        case KEY_GRAVE: return "`";
        default: return "?";
    }
}

// Key code from name
static int key_from_name(const char *name)
{
    if (!name || !name[0]) return 0;

    // Single letter keys
    if (strlen(name) == 1) {
        char c = (char)toupper(name[0]);
        if (c >= 'A' && c <= 'Z') {
            return KEY_A + (c - 'A');
        }
        if (c >= '0' && c <= '9') {
            return KEY_ZERO + (c - '0');
        }
        switch (c) {
            case '[': return KEY_LEFT_BRACKET;
            case ']': return KEY_RIGHT_BRACKET;
            case '-': return KEY_MINUS;
            case '=': return KEY_EQUAL;
            case ',': return KEY_COMMA;
            case '.': return KEY_PERIOD;
            case '/': return KEY_SLASH;
            case '\\': return KEY_BACKSLASH;
            case ';': return KEY_SEMICOLON;
            case '\'': return KEY_APOSTROPHE;
            case '`': return KEY_GRAVE;
        }
    }

    // Named keys (case insensitive)
    if (strcasecmp(name, "space") == 0) return KEY_SPACE;
    if (strcasecmp(name, "enter") == 0 || strcasecmp(name, "return") == 0) return KEY_ENTER;
    if (strcasecmp(name, "backspace") == 0) return KEY_BACKSPACE;
    if (strcasecmp(name, "tab") == 0) return KEY_TAB;
    if (strcasecmp(name, "escape") == 0 || strcasecmp(name, "esc") == 0) return KEY_ESCAPE;
    if (strcasecmp(name, "up") == 0) return KEY_UP;
    if (strcasecmp(name, "down") == 0) return KEY_DOWN;
    if (strcasecmp(name, "left") == 0) return KEY_LEFT;
    if (strcasecmp(name, "right") == 0) return KEY_RIGHT;
    if (strcasecmp(name, "home") == 0) return KEY_HOME;
    if (strcasecmp(name, "end") == 0) return KEY_END;
    if (strcasecmp(name, "pageup") == 0) return KEY_PAGE_UP;
    if (strcasecmp(name, "pagedown") == 0) return KEY_PAGE_DOWN;
    if (strcasecmp(name, "insert") == 0) return KEY_INSERT;
    if (strcasecmp(name, "delete") == 0 || strcasecmp(name, "del") == 0) return KEY_DELETE;

    // Function keys
    if (name[0] == 'F' || name[0] == 'f') {
        int num = atoi(name + 1);
        if (num >= 1 && num <= 12) {
            return KEY_F1 + (num - 1);
        }
    }

    return 0;
}

const char *keybindings_shortcut_string(const KeyBinding *binding)
{
    if (!binding) return "";

    shortcut_buffer[0] = '\0';

    // Add modifiers
    if (binding->modifiers & MOD_SUPER) {
        strcat(shortcut_buffer, "Cmd+");
    }
    if (binding->modifiers & MOD_CTRL) {
        strcat(shortcut_buffer, "Ctrl+");
    }
    if (binding->modifiers & MOD_ALT) {
        strcat(shortcut_buffer, "Alt+");
    }
    if (binding->modifiers & MOD_SHIFT) {
        strcat(shortcut_buffer, "Shift+");
    }

    // Add key name
    strcat(shortcut_buffer, key_name(binding->key));

    return shortcut_buffer;
}

bool keybindings_parse_shortcut(const char *shortcut, int *out_key, int *out_modifiers)
{
    if (!shortcut || !out_key || !out_modifiers) {
        return false;
    }

    *out_key = 0;
    *out_modifiers = MOD_NONE;

    char buffer[128];
    strncpy(buffer, shortcut, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // Parse each part separated by +
    char *token = strtok(buffer, "+");
    char *last_token = NULL;

    while (token) {
        // Trim whitespace
        while (*token && isspace(*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace(*end)) *end-- = '\0';

        // Check for modifier
        if (strcasecmp(token, "cmd") == 0 || strcasecmp(token, "super") == 0 ||
            strcasecmp(token, "command") == 0 || strcasecmp(token, "meta") == 0) {
            *out_modifiers |= MOD_SUPER;
        } else if (strcasecmp(token, "ctrl") == 0 || strcasecmp(token, "control") == 0) {
            *out_modifiers |= MOD_CTRL;
        } else if (strcasecmp(token, "shift") == 0) {
            *out_modifiers |= MOD_SHIFT;
        } else if (strcasecmp(token, "alt") == 0 || strcasecmp(token, "opt") == 0 ||
                   strcasecmp(token, "option") == 0) {
            *out_modifiers |= MOD_ALT;
        } else {
            // Assume it's the key
            last_token = token;
        }

        token = strtok(NULL, "+");
    }

    if (last_token) {
        *out_key = key_from_name(last_token);
    }

    return *out_key != 0;
}

#ifndef PALETTE_H
#define PALETTE_H

#include <stdbool.h>

#define PALETTE_MAX_COMMANDS 128
#define PALETTE_MAX_RECENT 10
#define PALETTE_INPUT_MAX 256
#define PALETTE_VISIBLE_ITEMS 12

// Forward declaration
struct App;

// Command callback type
typedef void (*CommandCallback)(struct App *app);

// Command definition
typedef struct PaletteCommand {
    const char *id;             // Unique command ID
    const char *name;           // Display name
    const char *category;       // Category (File, Edit, View, etc.)
    const char *shortcut;       // Keyboard shortcut string (e.g., "Cmd+N")
    CommandCallback callback;   // Function to execute
    bool requires_selection;    // Requires file selection to work
} PaletteCommand;

// Command palette state
typedef struct PaletteState {
    bool visible;
    char input[PALETTE_INPUT_MAX];
    int input_cursor;
    int selected_index;
    int scroll_offset;

    // Command registry
    PaletteCommand commands[PALETTE_MAX_COMMANDS];
    int command_count;

    // Filtered results (indices into commands array)
    int filtered[PALETTE_MAX_COMMANDS];
    int filtered_count;

    // Recent commands (IDs)
    char recent[PALETTE_MAX_RECENT][64];
    int recent_count;

    // Animation
    float animation_progress;
} PaletteState;

// Initialize command palette
void palette_init(PaletteState *palette);

// Free palette resources
void palette_free(PaletteState *palette);

// Register a command
void palette_register(PaletteState *palette, const char *id, const char *name,
                      const char *category, const char *shortcut,
                      CommandCallback callback, bool requires_selection);

// Register all built-in commands
void palette_register_builtins(PaletteState *palette);

// Show/hide palette
void palette_show(PaletteState *palette);
void palette_hide(PaletteState *palette);
void palette_toggle(PaletteState *palette);

// Check if palette is visible
bool palette_is_visible(PaletteState *palette);

// Update palette state
void palette_update(PaletteState *palette, float delta_time);

// Handle input when palette is visible
void palette_handle_input(struct App *app);

// Draw the command palette
void palette_draw(struct App *app);

// Execute selected command
void palette_execute_selected(struct App *app);

// Execute command by ID
bool palette_execute(struct App *app, const char *command_id);

// Update filtered list based on input
void palette_filter(PaletteState *palette);

// Fuzzy match score (0 = no match, higher = better match)
int palette_fuzzy_score(const char *query, const char *text);

#endif // PALETTE_H

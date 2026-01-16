#ifndef COMMAND_BAR_H
#define COMMAND_BAR_H

#include "raylib.h"
#include "../api/claude_client.h"
#include "../api/auth.h"
#include "../tools/tool_registry.h"
#include "../tools/tool_executor.h"
#include "progress_indicator.h"
#include <stdbool.h>

// Forward declarations for AI modules
struct SemanticSearch;
struct VisualSearch;
struct GeminiClient;

#define COMMAND_BAR_MAX_INPUT 1024
#define COMMAND_BAR_MAX_HISTORY 32
#define COMMAND_BAR_HEIGHT 40
#define CONFIRMATION_MAX_OPERATIONS 8

// Command bar state
typedef enum CommandBarState {
    CMD_STATE_HIDDEN,
    CMD_STATE_INPUT,
    CMD_STATE_LOADING,
    CMD_STATE_CONFIRMING,
    CMD_STATE_RESULT,
    CMD_STATE_ERROR
} CommandBarState;

// Confirmation dialog state
typedef struct ConfirmationState {
    PendingOperation operations[CONFIRMATION_MAX_OPERATIONS];
    int operation_count;
    int selected_index;
    bool confirmed;
    bool cancelled;
} ConfirmationState;

// Command history entry
typedef struct CommandHistoryEntry {
    char input[COMMAND_BAR_MAX_INPUT];
    char response[2048];
    bool success;
} CommandHistoryEntry;

// Command bar
typedef struct CommandBar {
    // State
    CommandBarState state;
    bool visible;
    bool focused;

    // Input
    char input[COMMAND_BAR_MAX_INPUT];
    int cursor_pos;
    int selection_start;
    int selection_end;

    // AI clients
    ClaudeClient *claude;
    struct GeminiClient *gemini;
    AuthState auth;
    ToolRegistry *registry;
    ToolExecutor *executor;

    // Current request/response
    ClaudeMessageRequest request;
    ClaudeMessageResponse response;

    // Confirmation
    ConfirmationState confirmation;

    // Result display
    char result_message[1024];
    float result_timer;

    // History
    CommandHistoryEntry history[COMMAND_BAR_MAX_HISTORY];
    int history_count;
    int history_index;

    // Animation
    float animation_progress;
    float cursor_blink_timer;
    bool cursor_visible;
    ProgressIndicator ai_progress;  // Progress indicator for AI operations

    // UI dimensions (set during draw)
    Rectangle bounds;

    // Flag to signal that directory needs refresh after file operations
    bool needs_directory_refresh;
} CommandBar;

// Initialize command bar
void command_bar_init(CommandBar *bar);

// Free command bar resources
void command_bar_free(CommandBar *bar);

// Load authentication and set up Claude client
bool command_bar_load_auth(CommandBar *bar, const char *config_path);

// Show the command bar (Cmd+K)
void command_bar_show(CommandBar *bar);

// Hide the command bar
void command_bar_hide(CommandBar *bar);

// Toggle visibility
void command_bar_toggle(CommandBar *bar);

// Check if command bar is active (should capture input)
bool command_bar_is_active(CommandBar *bar);

// Update command bar state
void command_bar_update(CommandBar *bar, float delta_time);

// Handle keyboard input when focused
void command_bar_handle_input(CommandBar *bar);

// Draw the command bar
void command_bar_draw(CommandBar *bar, int window_width, int window_height);

// Set current directory for tool execution
void command_bar_set_current_dir(CommandBar *bar, const char *path);

// Set AI search contexts for semantic and visual search tools
void command_bar_set_semantic_search(CommandBar *bar, struct SemanticSearch *search);
void command_bar_set_visual_search(CommandBar *bar, struct VisualSearch *search);

// Load Gemini authentication and set up image generation client
bool command_bar_load_gemini_auth(CommandBar *bar, const char *config_path);

// Submit current input
void command_bar_submit(CommandBar *bar);

// Confirm pending operations
void command_bar_confirm(CommandBar *bar);

// Cancel pending operations
void command_bar_cancel(CommandBar *bar);

// Get the system prompt for file operations
const char *command_bar_get_system_prompt(const char *current_dir);

// Check if directory refresh is needed and clear the flag
bool command_bar_needs_refresh(CommandBar *bar);

#endif // COMMAND_BAR_H

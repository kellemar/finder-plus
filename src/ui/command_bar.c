#include "command_bar.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "../api/gemini_client.h"
#include "../../external/cJSON/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Debug logging for command bar (set to 1 or use -DCMDBAR_DEBUG=1)
#ifndef CMDBAR_DEBUG
#define CMDBAR_DEBUG 0
#endif

#if CMDBAR_DEBUG
#define CMDBAR_LOG(fmt, ...) fprintf(stderr, "[CMDBAR] " fmt "\n", ##__VA_ARGS__)
#else
#define CMDBAR_LOG(fmt, ...) ((void)0)
#endif

#define INPUT_PADDING 12
#define CMDBAR_FONT_SIZE 16
#define RESULT_DISPLAY_TIME 3.0f
#define CURSOR_BLINK_RATE 0.5f

static char system_prompt_buffer[4096];

void command_bar_init(CommandBar *bar)
{
    if (!bar) return;
    memset(bar, 0, sizeof(CommandBar));

    bar->state = CMD_STATE_HIDDEN;
    bar->visible = false;
    bar->cursor_visible = true;
    bar->history_index = -1;
    bar->needs_directory_refresh = false;

    // Create tool registry with file tools
    bar->registry = tool_registry_create();
    if (bar->registry) {
        tool_registry_register_file_tools(bar->registry);
    }

    // Create tool executor
    bar->executor = tool_executor_create(bar->registry);

    // Initialize AI progress indicator
    progress_indicator_init(&bar->ai_progress, PROGRESS_SPINNER);
    progress_indicator_set_message(&bar->ai_progress, "Thinking...");

    auth_init(&bar->auth);
}

void command_bar_free(CommandBar *bar)
{
    if (!bar) return;

    if (bar->claude) {
        claude_client_destroy(bar->claude);
        bar->claude = NULL;
    }

    if (bar->gemini) {
        gemini_client_destroy(bar->gemini);
        bar->gemini = NULL;
    }

    if (bar->executor) {
        tool_executor_destroy(bar->executor);
        bar->executor = NULL;
    }

    if (bar->registry) {
        tool_registry_destroy(bar->registry);
        bar->registry = NULL;
    }

    claude_request_cleanup(&bar->request);
    claude_response_cleanup(&bar->response);
    auth_clear(&bar->auth);
}

bool command_bar_load_auth(CommandBar *bar, const char *config_path)
{
    if (!bar) return false;

    if (!auth_load(&bar->auth, config_path)) {
        return false;
    }

    bar->claude = claude_client_create(bar->auth.api_key);
    return bar->claude != NULL;
}

bool command_bar_load_gemini_auth(CommandBar *bar, const char *config_path)
{
    CMDBAR_LOG("=== command_bar_load_gemini_auth START ===");
    if (!bar) {
        CMDBAR_LOG("ERROR: NULL bar");
        return false;
    }

    if (!auth_load_gemini(&bar->auth, config_path)) {
        CMDBAR_LOG("ERROR: Failed to load Gemini auth (config_path=%s)",
                   config_path ? config_path : "NULL");
        return false;
    }

    CMDBAR_LOG("Gemini auth loaded, API key length: %zu", strlen(bar->auth.gemini_api_key));

    bar->gemini = gemini_client_create(bar->auth.gemini_api_key);
    if (bar->gemini && bar->executor) {
        CMDBAR_LOG("Setting Gemini client on tool executor");
        tool_executor_set_gemini_client(bar->executor, bar->gemini);
    } else {
        CMDBAR_LOG("WARNING: Could not set Gemini client (gemini=%p, executor=%p)",
                   (void*)bar->gemini, (void*)bar->executor);
    }

    CMDBAR_LOG("=== command_bar_load_gemini_auth END (success=%d) ===", bar->gemini != NULL);
    return bar->gemini != NULL;
}

void command_bar_show(CommandBar *bar)
{
    if (!bar) return;
    bar->visible = true;
    bar->focused = true;
    bar->state = CMD_STATE_INPUT;
    bar->cursor_pos = (int)strlen(bar->input);
    bar->animation_progress = 0.0f;
}

void command_bar_hide(CommandBar *bar)
{
    if (!bar) return;
    bar->visible = false;
    bar->focused = false;

    if (bar->state == CMD_STATE_INPUT || bar->state == CMD_STATE_ERROR) {
        bar->state = CMD_STATE_HIDDEN;
    }
}

void command_bar_toggle(CommandBar *bar)
{
    if (!bar) return;
    if (bar->visible) {
        command_bar_hide(bar);
    } else {
        command_bar_show(bar);
    }
}

bool command_bar_is_active(CommandBar *bar)
{
    return bar && bar->visible && bar->focused;
}

void command_bar_update(CommandBar *bar, float delta_time)
{
    if (!bar) return;

    // Animation
    if (bar->visible && bar->animation_progress < 1.0f) {
        bar->animation_progress += delta_time * 4.0f;
        if (bar->animation_progress > 1.0f) bar->animation_progress = 1.0f;
    }

    // Cursor blink
    bar->cursor_blink_timer += delta_time;
    if (bar->cursor_blink_timer >= CURSOR_BLINK_RATE) {
        bar->cursor_blink_timer = 0.0f;
        bar->cursor_visible = !bar->cursor_visible;
    }

    // Update AI progress indicator animation
    if (bar->state == CMD_STATE_LOADING) {
        progress_indicator_update(&bar->ai_progress, delta_time);
    }

    // Result timer
    if (bar->state == CMD_STATE_RESULT) {
        bar->result_timer -= delta_time;
        if (bar->result_timer <= 0.0f) {
            bar->state = CMD_STATE_INPUT;
            bar->input[0] = '\0';
            bar->cursor_pos = 0;
        }
    }
}

void command_bar_handle_input(CommandBar *bar)
{
    if (!bar || !bar->visible || !bar->focused) return;

    // Handle escape
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (bar->state == CMD_STATE_CONFIRMING) {
            command_bar_cancel(bar);
        } else {
            command_bar_hide(bar);
        }
        return;
    }

    // Handle confirmation state
    if (bar->state == CMD_STATE_CONFIRMING) {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            command_bar_confirm(bar);
        } else if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_K)) {
            if (bar->confirmation.selected_index > 0) {
                bar->confirmation.selected_index--;
            }
        } else if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_J)) {
            if (bar->confirmation.selected_index < bar->confirmation.operation_count - 1) {
                bar->confirmation.selected_index++;
            }
        }
        return;
    }

    // Input state
    if (bar->state != CMD_STATE_INPUT) return;

    // Handle enter to submit
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        if (strlen(bar->input) > 0) {
            command_bar_submit(bar);
        }
        return;
    }

    // Handle backspace
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        if (bar->cursor_pos > 0) {
            int len = (int)strlen(bar->input);
            memmove(&bar->input[bar->cursor_pos - 1], &bar->input[bar->cursor_pos], (size_t)(len - bar->cursor_pos + 1));
            bar->cursor_pos--;
        }
        return;
    }

    // Handle delete
    if (IsKeyPressed(KEY_DELETE)) {
        int len = (int)strlen(bar->input);
        if (bar->cursor_pos < len) {
            memmove(&bar->input[bar->cursor_pos], &bar->input[bar->cursor_pos + 1], (size_t)(len - bar->cursor_pos));
        }
        return;
    }

    // Handle cursor movement
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
        if (bar->cursor_pos > 0) bar->cursor_pos--;
        bar->cursor_visible = true;
        bar->cursor_blink_timer = 0.0f;
        return;
    }

    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
        int len = (int)strlen(bar->input);
        if (bar->cursor_pos < len) bar->cursor_pos++;
        bar->cursor_visible = true;
        bar->cursor_blink_timer = 0.0f;
        return;
    }

    // Handle home/end
    if (IsKeyPressed(KEY_HOME)) {
        bar->cursor_pos = 0;
        return;
    }

    if (IsKeyPressed(KEY_END)) {
        bar->cursor_pos = (int)strlen(bar->input);
        return;
    }

    // Handle history navigation
    if (IsKeyPressed(KEY_UP)) {
        if (bar->history_count > 0 && bar->history_index < bar->history_count - 1) {
            bar->history_index++;
            strcpy(bar->input, bar->history[bar->history_index].input);
            bar->cursor_pos = (int)strlen(bar->input);
        }
        return;
    }

    if (IsKeyPressed(KEY_DOWN)) {
        if (bar->history_index > 0) {
            bar->history_index--;
            strcpy(bar->input, bar->history[bar->history_index].input);
            bar->cursor_pos = (int)strlen(bar->input);
        } else if (bar->history_index == 0) {
            bar->history_index = -1;
            bar->input[0] = '\0';
            bar->cursor_pos = 0;
        }
        return;
    }

    // Handle text input
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key <= 126) {
            int len = (int)strlen(bar->input);
            if (len < COMMAND_BAR_MAX_INPUT - 1) {
                memmove(&bar->input[bar->cursor_pos + 1], &bar->input[bar->cursor_pos], (size_t)(len - bar->cursor_pos + 1));
                bar->input[bar->cursor_pos] = (char)key;
                bar->cursor_pos++;
            }
        }
        key = GetCharPressed();
    }
}

const char *command_bar_get_system_prompt(const char *current_dir)
{
    snprintf(system_prompt_buffer, sizeof(system_prompt_buffer),
        "You are an AI file management assistant integrated into Finder Plus, a macOS file manager. "
        "The user is currently viewing the directory: %s\n\n"
        "You have access to the following tools for file operations:\n"
        "- file_list: List files in a directory\n"
        "- file_move: Move files to a new location\n"
        "- file_copy: Copy files to a new location\n"
        "- file_delete: Move files to Trash\n"
        "- file_create: Create new files or directories\n"
        "- file_rename: Rename a file or directory\n"
        "- file_search: Search for files by pattern\n"
        "- file_metadata: Get detailed info about a file\n"
        "- batch_rename: Rename multiple files with find/replace\n"
        "- batch_move: Move and organize multiple files\n\n"
        "AI-Powered Search Tools (use when user describes content or visual appearance):\n"
        "- semantic_search: Find files by their content meaning (e.g., 'find documents about machine learning')\n"
        "- visual_search: Find images by description (e.g., 'find photos of sunset at beach')\n"
        "- similar_images: Find images visually similar to a given image\n\n"
        "AI Image Generation:\n"
        "- image_generate: Generate an image from a text description using Gemini AI and save it to the current directory\n\n"
        "When the user asks you to perform file operations:\n"
        "1. Use the appropriate tool(s) to accomplish the task\n"
        "2. Be careful with destructive operations (delete, move)\n"
        "3. For ambiguous requests, list files first to understand the context\n"
        "4. Use relative paths from the current directory when possible\n"
        "5. Confirm understanding before batch operations on many files\n"
        "6. Use semantic_search for content-based queries, visual_search for image queries\n\n"
        "Always respond concisely and use tools to take action rather than just describing what you would do.",
        current_dir ? current_dir : "/");

    return system_prompt_buffer;
}

void command_bar_submit(CommandBar *bar)
{
    CMDBAR_LOG("=== command_bar_submit START ===");

    if (!bar || strlen(bar->input) == 0) {
        CMDBAR_LOG("ERROR: NULL bar or empty input");
        return;
    }

    CMDBAR_LOG("User input: %s", bar->input);
    CMDBAR_LOG("Gemini client configured: %s", bar->gemini ? "YES" : "NO");

    // Check auth
    if (!bar->claude || !auth_is_ready(&bar->auth)) {
        CMDBAR_LOG("ERROR: Claude API not ready");
        strncpy(bar->result_message, "API key not configured. Set CLAUDE_API_KEY environment variable.", sizeof(bar->result_message) - 1);
        bar->state = CMD_STATE_ERROR;
        bar->result_timer = RESULT_DISPLAY_TIME;
        return;
    }

    bar->state = CMD_STATE_LOADING;

    // Build request
    claude_request_init(&bar->request);
    claude_request_set_system_prompt(&bar->request, command_bar_get_system_prompt(bar->executor ? bar->executor->current_dir : NULL));
    claude_request_add_user_message(&bar->request, bar->input);

    // Add tools
    cJSON *tools = tool_registry_to_json(bar->registry);
    claude_request_set_tools(&bar->request, tools);

    CMDBAR_LOG("Sending request to Claude...");

    // Send request
    claude_response_init(&bar->response);
    bool success = claude_send_message(bar->claude, &bar->request, &bar->response);

    if (tools) cJSON_Delete(tools);

    CMDBAR_LOG("Claude response: success=%d", success);

    if (!success) {
        CMDBAR_LOG("ERROR: Claude request failed: %s", bar->response.error ? bar->response.error : "unknown");
        snprintf(bar->result_message, sizeof(bar->result_message), "Error: %s",
                 bar->response.error ? bar->response.error : "Request failed");
        bar->state = CMD_STATE_ERROR;
        bar->result_timer = RESULT_DISPLAY_TIME;
        claude_request_cleanup(&bar->request);
        return;
    }

    CMDBAR_LOG("Claude response content: %.200s%s",
               bar->response.content ? bar->response.content : "(null)",
               bar->response.content && strlen(bar->response.content) > 200 ? "..." : "");

    // Check for tool use
    if (claude_response_has_tool_use(&bar->response)) {
        CMDBAR_LOG("Claude wants to use %d tools", bar->response.tool_use_count);

        // Prepare pending operations for confirmation
        bar->confirmation.operation_count = 0;
        bar->confirmation.selected_index = 0;
        bar->confirmation.confirmed = false;
        bar->confirmation.cancelled = false;

        for (int i = 0; i < bar->response.tool_use_count && i < CONFIRMATION_MAX_OPERATIONS; i++) {
            ClaudeToolUse *tool_use = &bar->response.tool_uses[i];
            CMDBAR_LOG("Tool %d: name=%s, id=%s", i, tool_use->name, tool_use->id);
            CMDBAR_LOG("Tool %d input: %.200s%s", i, tool_use->input,
                       strlen(tool_use->input) > 200 ? "..." : "");

            tool_executor_prepare(bar->executor, tool_use->name, tool_use->id,
                                  tool_use->input, &bar->confirmation.operations[i]);
            bar->confirmation.operation_count++;
        }

        bar->state = CMD_STATE_CONFIRMING;
        CMDBAR_LOG("Entering CONFIRMING state with %d operations", bar->confirmation.operation_count);
    } else {
        CMDBAR_LOG("No tool use - just text response");
        // Just a text response
        strncpy(bar->result_message, bar->response.content, sizeof(bar->result_message) - 1);
        bar->state = CMD_STATE_RESULT;
        bar->result_timer = RESULT_DISPLAY_TIME;

        // Add to history
        if (bar->history_count < COMMAND_BAR_MAX_HISTORY) {
            memmove(&bar->history[1], &bar->history[0], sizeof(CommandHistoryEntry) * (size_t)bar->history_count);
            strncpy(bar->history[0].input, bar->input, COMMAND_BAR_MAX_INPUT - 1);
            strncpy(bar->history[0].response, bar->result_message, sizeof(bar->history[0].response) - 1);
            bar->history[0].success = true;
            bar->history_count++;
        }
    }

    claude_request_cleanup(&bar->request);
    CMDBAR_LOG("=== command_bar_submit END ===");
}

void command_bar_confirm(CommandBar *bar)
{
    CMDBAR_LOG("=== command_bar_confirm START ===");

    if (!bar || bar->state != CMD_STATE_CONFIRMING) {
        CMDBAR_LOG("ERROR: Invalid bar or state");
        return;
    }

    CMDBAR_LOG("Confirming %d operations", bar->confirmation.operation_count);

    int success_count = 0;
    char results[2048] = "";
    size_t offset = 0;

    for (int i = 0; i < bar->confirmation.operation_count; i++) {
        PendingOperation *op = &bar->confirmation.operations[i];
        CMDBAR_LOG("Executing operation %d: tool=%s", i, op->tool_name);

        ToolResult result = tool_executor_confirm(bar->executor, op);

        CMDBAR_LOG("Operation %d result: success=%d", i, result.success);
        if (result.success) {
            success_count++;
            if (result.output) {
                CMDBAR_LOG("Operation %d output: %s", i, result.output);
                offset += (size_t)snprintf(results + offset, sizeof(results) - offset,
                                           "%s\n", result.output);
            }
        } else {
            CMDBAR_LOG("Operation %d error: %s", i, result.error ? result.error : "Unknown");
            offset += (size_t)snprintf(results + offset, sizeof(results) - offset,
                                       "Failed: %s\n", result.error ? result.error : "Unknown error");
        }

        tool_result_cleanup(&result);
    }

    snprintf(bar->result_message, sizeof(bar->result_message),
             "Completed %d of %d operations.\n%s",
             success_count, bar->confirmation.operation_count, results);

    CMDBAR_LOG("Final result: %s", bar->result_message);

    bar->state = CMD_STATE_RESULT;
    bar->result_timer = RESULT_DISPLAY_TIME;

    // Signal that directory needs to be refreshed to show new/modified files
    if (success_count > 0) {
        bar->needs_directory_refresh = true;
        CMDBAR_LOG("Directory refresh flagged");
    }

    // Add to history
    if (bar->history_count < COMMAND_BAR_MAX_HISTORY) {
        memmove(&bar->history[1], &bar->history[0], sizeof(CommandHistoryEntry) * (size_t)bar->history_count);
        strncpy(bar->history[0].input, bar->input, COMMAND_BAR_MAX_INPUT - 1);
        strncpy(bar->history[0].response, bar->result_message, sizeof(bar->history[0].response) - 1);
        bar->history[0].success = success_count == bar->confirmation.operation_count;
        bar->history_count++;
    }

    bar->input[0] = '\0';
    bar->cursor_pos = 0;

    CMDBAR_LOG("=== command_bar_confirm END ===");
}

void command_bar_cancel(CommandBar *bar)
{
    if (!bar) return;

    for (int i = 0; i < bar->confirmation.operation_count; i++) {
        tool_executor_cancel(&bar->confirmation.operations[i]);
    }

    bar->confirmation.operation_count = 0;
    bar->state = CMD_STATE_INPUT;
    strncpy(bar->result_message, "Operation cancelled", sizeof(bar->result_message) - 1);
    bar->result_timer = 1.0f;
}

void command_bar_set_current_dir(CommandBar *bar, const char *path)
{
    if (!bar || !bar->executor) return;
    tool_executor_set_cwd(bar->executor, path);
}

void command_bar_set_semantic_search(CommandBar *bar, struct SemanticSearch *search)
{
    if (!bar || !bar->executor) return;
    tool_executor_set_semantic_search(bar->executor, search);
}

void command_bar_set_visual_search(CommandBar *bar, struct VisualSearch *search)
{
    if (!bar || !bar->executor) return;
    tool_executor_set_visual_search(bar->executor, search);
}

void command_bar_draw(CommandBar *bar, int window_width, int window_height)
{
    if (!bar || !bar->visible) return;

    Theme *theme = theme_get_current();

    // Calculate position (centered, near top)
    int bar_width = window_width > 800 ? 600 : window_width - 40;
    int bar_x = (window_width - bar_width) / 2;
    int bar_y = 80;

    // Apply animation
    float alpha = bar->animation_progress;
    bar_y = (int)(bar_y - (1.0f - alpha) * 20);

    bar->bounds = (Rectangle){ (float)bar_x, (float)bar_y, (float)bar_width, COMMAND_BAR_HEIGHT };

    // Draw backdrop
    Color backdrop = { 0, 0, 0, (unsigned char)(150 * alpha) };
    DrawRectangle(0, 0, window_width, window_height, backdrop);

    // Draw command bar background
    Color bar_bg = theme->background;
    bar_bg.a = (unsigned char)(bar_bg.a * alpha);
    DrawRectangleRounded(bar->bounds, 0.2f, 8, bar_bg);

    // Draw border
    Color border = theme->border;
    border.a = (unsigned char)(border.a * alpha);
    DrawRectangleRoundedLinesEx(bar->bounds, 0.2f, 8, 2.0f, border);

    // Draw AI indicator
    Color ai_color = theme->aiAccent;
    ai_color.a = (unsigned char)(ai_color.a * alpha);
    DrawCircle(bar_x + 20, bar_y + COMMAND_BAR_HEIGHT / 2, 6, ai_color);

    // Draw state-specific content
    switch (bar->state) {
        case CMD_STATE_INPUT: {
            // Draw input text
            Color text_color = theme->textPrimary;
            text_color.a = (unsigned char)(text_color.a * alpha);

            int text_x = bar_x + 40;
            int text_y = bar_y + (COMMAND_BAR_HEIGHT - CMDBAR_FONT_SIZE) / 2;

            if (strlen(bar->input) > 0) {
                DrawTextCustom(bar->input, text_x, text_y, CMDBAR_FONT_SIZE, text_color);
            } else {
                Color placeholder = theme->textSecondary;
                placeholder.a = (unsigned char)(placeholder.a * alpha * 0.6f);
                DrawTextCustom("Ask Claude to manage your files...", text_x, text_y, CMDBAR_FONT_SIZE, placeholder);
            }

            // Draw cursor
            if (bar->cursor_visible && bar->focused) {
                int cursor_x = text_x + MeasureTextCustom(bar->input, CMDBAR_FONT_SIZE);
                if (bar->cursor_pos < (int)strlen(bar->input)) {
                    char temp[COMMAND_BAR_MAX_INPUT];
                    strncpy(temp, bar->input, (size_t)bar->cursor_pos);
                    temp[bar->cursor_pos] = '\0';
                    cursor_x = text_x + MeasureTextCustom(temp, CMDBAR_FONT_SIZE);
                }
                DrawRectangle(cursor_x, text_y, 2, CMDBAR_FONT_SIZE, text_color);
            }
            break;
        }

        case CMD_STATE_LOADING: {
            // Draw spinner on the left
            int spinner_cx = bar_x + 30;
            int spinner_cy = bar_y + COMMAND_BAR_HEIGHT / 2;
            Color spinner_color = theme->aiAccent;
            spinner_color.a = (unsigned char)(spinner_color.a * alpha);
            progress_indicator_draw_spinner(&bar->ai_progress, spinner_cx, spinner_cy, 10, spinner_color);

            // Draw message to the right of spinner
            Color text_color = theme->textSecondary;
            text_color.a = (unsigned char)(text_color.a * alpha);
            DrawTextCustom(bar->ai_progress.message, bar_x + 55,
                           bar_y + (COMMAND_BAR_HEIGHT - CMDBAR_FONT_SIZE) / 2,
                           CMDBAR_FONT_SIZE, text_color);
            break;
        }

        case CMD_STATE_CONFIRMING: {
            // Draw expanded confirmation dialog
            int dialog_height = 60 + bar->confirmation.operation_count * 30;
            Rectangle dialog = { (float)bar_x, (float)bar_y, (float)bar_width, (float)dialog_height };

            DrawRectangleRounded(dialog, 0.1f, 8, bar_bg);
            DrawRectangleRoundedLinesEx(dialog, 0.1f, 8, 2.0f, border);

            Color text_color = theme->textPrimary;
            text_color.a = (unsigned char)(text_color.a * alpha);
            Color secondary = theme->textSecondary;
            secondary.a = (unsigned char)(secondary.a * alpha);

            DrawTextCustom("Confirm operations:", bar_x + 15, bar_y + 10, CMDBAR_FONT_SIZE, text_color);

            for (int i = 0; i < bar->confirmation.operation_count; i++) {
                int item_y = bar_y + 35 + i * 30;
                Color item_color = i == bar->confirmation.selected_index ? ai_color : secondary;

                char prefix = i == bar->confirmation.selected_index ? '>' : ' ';
                char item_text[600];
                snprintf(item_text, sizeof(item_text), "%c %s", prefix, bar->confirmation.operations[i].description);
                DrawTextCustom(item_text, bar_x + 20, item_y, CMDBAR_FONT_SIZE - 2, item_color);
            }

            // Instructions
            DrawTextCustom("[Enter] Confirm  [Esc] Cancel", bar_x + 15, bar_y + dialog_height - 20, CMDBAR_FONT_SIZE - 4, secondary);
            break;
        }

        case CMD_STATE_RESULT:
        case CMD_STATE_ERROR: {
            Color text_color = bar->state == CMD_STATE_ERROR ? theme->error : theme->success;
            text_color.a = (unsigned char)(text_color.a * alpha);

            // Truncate message to fit
            char display_msg[256];
            strncpy(display_msg, bar->result_message, 255);
            display_msg[255] = '\0';

            char *newline = strchr(display_msg, '\n');
            if (newline) *newline = '\0';

            DrawTextCustom(display_msg, bar_x + 40, bar_y + (COMMAND_BAR_HEIGHT - CMDBAR_FONT_SIZE) / 2, CMDBAR_FONT_SIZE, text_color);
            break;
        }

        default:
            break;
    }
}

bool command_bar_needs_refresh(CommandBar *bar)
{
    if (!bar) return false;

    bool needs_refresh = bar->needs_directory_refresh;
    if (needs_refresh) {
        bar->needs_directory_refresh = false;
        CMDBAR_LOG("Directory refresh consumed");
    }
    return needs_refresh;
}

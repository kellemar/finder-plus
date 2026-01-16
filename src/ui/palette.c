#include "palette.h"
#include "../app.h"
#include "../utils/font.h"
#include "tabs.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PALETTE_WIDTH 600
#define PALETTE_HEIGHT 400
#define PALETTE_INPUT_HEIGHT 40
#define PALETTE_ITEM_HEIGHT 32
#define PALETTE_ANIMATION_SPEED 12.0f
#define PALETTE_PADDING 12

// Built-in command callbacks (forward declarations)
static void cmd_new_file(struct App *app);
static void cmd_new_folder(struct App *app);
static void cmd_open(struct App *app);
static void cmd_copy(struct App *app);
static void cmd_cut(struct App *app);
static void cmd_paste(struct App *app);
static void cmd_delete(struct App *app);
static void cmd_rename(struct App *app);
static void cmd_duplicate(struct App *app);
static void cmd_select_all(struct App *app);
static void cmd_view_list(struct App *app);
static void cmd_view_grid(struct App *app);
static void cmd_view_columns(struct App *app);
static void cmd_toggle_hidden(struct App *app);
static void cmd_toggle_preview(struct App *app);
static void cmd_go_back(struct App *app);
static void cmd_go_forward(struct App *app);
static void cmd_go_parent(struct App *app);
static void cmd_go_home(struct App *app);
static void cmd_refresh(struct App *app);
static void cmd_new_tab(struct App *app);
static void cmd_close_tab(struct App *app);
static void cmd_toggle_sidebar(struct App *app);
static void cmd_toggle_fullscreen(struct App *app);
static void cmd_toggle_theme(struct App *app);
static void cmd_show_queue(struct App *app);

void palette_init(PaletteState *palette)
{
    palette->visible = false;
    palette->input[0] = '\0';
    palette->input_cursor = 0;
    palette->selected_index = 0;
    palette->scroll_offset = 0;
    palette->command_count = 0;
    palette->filtered_count = 0;
    palette->recent_count = 0;
    palette->animation_progress = 0.0f;
}

void palette_free(PaletteState *palette)
{
    // Nothing to free currently (all static allocations)
    (void)palette;
}

void palette_register(PaletteState *palette, const char *id, const char *name,
                      const char *category, const char *shortcut,
                      CommandCallback callback, bool requires_selection)
{
    if (palette->command_count >= PALETTE_MAX_COMMANDS) {
        return;
    }

    PaletteCommand *cmd = &palette->commands[palette->command_count];
    cmd->id = id;
    cmd->name = name;
    cmd->category = category;
    cmd->shortcut = shortcut;
    cmd->callback = callback;
    cmd->requires_selection = requires_selection;

    palette->command_count++;
}

void palette_register_builtins(PaletteState *palette)
{
    // File commands
    palette_register(palette, "file.new", "New File", "File", "Cmd+N", cmd_new_file, false);
    palette_register(palette, "file.new_folder", "New Folder", "File", "Cmd+Shift+N", cmd_new_folder, false);
    palette_register(palette, "file.open", "Open", "File", "Cmd+O", cmd_open, true);
    palette_register(palette, "file.copy", "Copy", "File", "Cmd+C", cmd_copy, true);
    palette_register(palette, "file.cut", "Cut", "File", "Cmd+X", cmd_cut, true);
    palette_register(palette, "file.paste", "Paste", "File", "Cmd+V", cmd_paste, false);
    palette_register(palette, "file.delete", "Delete", "File", "Cmd+Delete", cmd_delete, true);
    palette_register(palette, "file.rename", "Rename", "File", "Enter", cmd_rename, true);
    palette_register(palette, "file.duplicate", "Duplicate", "File", "Cmd+D", cmd_duplicate, true);

    // Edit commands
    palette_register(palette, "edit.select_all", "Select All", "Edit", "Cmd+A", cmd_select_all, false);

    // View commands
    palette_register(palette, "view.list", "List View", "View", "Cmd+1", cmd_view_list, false);
    palette_register(palette, "view.grid", "Grid View", "View", "Cmd+2", cmd_view_grid, false);
    palette_register(palette, "view.columns", "Column View", "View", "Cmd+3", cmd_view_columns, false);
    palette_register(palette, "view.hidden", "Toggle Hidden Files", "View", "Cmd+.", cmd_toggle_hidden, false);
    palette_register(palette, "view.preview", "Toggle Preview", "View", "Cmd+Shift+P", cmd_toggle_preview, false);
    palette_register(palette, "view.sidebar", "Toggle Sidebar", "View", "Cmd+\\", cmd_toggle_sidebar, false);
    palette_register(palette, "view.fullscreen", "Toggle Fullscreen", "View", "Cmd+Enter", cmd_toggle_fullscreen, false);
    palette_register(palette, "view.theme", "Toggle Theme", "View", NULL, cmd_toggle_theme, false);

    // Navigation commands
    palette_register(palette, "nav.back", "Go Back", "Navigate", "Cmd+[", cmd_go_back, false);
    palette_register(palette, "nav.forward", "Go Forward", "Navigate", "Cmd+]", cmd_go_forward, false);
    palette_register(palette, "nav.parent", "Go to Parent", "Navigate", "Cmd+Up", cmd_go_parent, false);
    palette_register(palette, "nav.home", "Go to Home", "Navigate", "Cmd+Shift+H", cmd_go_home, false);
    palette_register(palette, "nav.refresh", "Refresh", "Navigate", "Cmd+R", cmd_refresh, false);

    // Tab commands
    palette_register(palette, "tab.new", "New Tab", "Tab", "Cmd+T", cmd_new_tab, false);
    palette_register(palette, "tab.close", "Close Tab", "Tab", "Cmd+W", cmd_close_tab, false);

    // Window commands
    palette_register(palette, "window.queue", "Show Operation Queue", "Window", "Cmd+Shift+Q", cmd_show_queue, false);
}

void palette_show(PaletteState *palette)
{
    palette->visible = true;
    palette->input[0] = '\0';
    palette->input_cursor = 0;
    palette->selected_index = 0;
    palette->scroll_offset = 0;
    palette_filter(palette);
}

void palette_hide(PaletteState *palette)
{
    palette->visible = false;
}

void palette_toggle(PaletteState *palette)
{
    if (palette->visible) {
        palette_hide(palette);
    } else {
        palette_show(palette);
    }
}

bool palette_is_visible(PaletteState *palette)
{
    return palette->visible || palette->animation_progress > 0.0f;
}

void palette_update(PaletteState *palette, float delta_time)
{
    float target = palette->visible ? 1.0f : 0.0f;
    if (palette->animation_progress < target) {
        palette->animation_progress += PALETTE_ANIMATION_SPEED * delta_time;
        if (palette->animation_progress > target) {
            palette->animation_progress = target;
        }
    } else if (palette->animation_progress > target) {
        palette->animation_progress -= PALETTE_ANIMATION_SPEED * delta_time;
        if (palette->animation_progress < target) {
            palette->animation_progress = target;
        }
    }
}

int palette_fuzzy_score(const char *query, const char *text)
{
    if (!query || !text) {
        return 0; // NULL inputs return 0
    }
    if (query[0] == '\0') {
        return text ? 1 : 0; // Empty query matches everything with low score
    }

    int score = 0;
    int consecutive = 0;
    int query_len = (int)strlen(query);
    int text_len = (int)strlen(text);
    int qi = 0;

    for (int ti = 0; ti < text_len && qi < query_len; ti++) {
        char qc = (char)tolower(query[qi]);
        char tc = (char)tolower(text[ti]);

        if (qc == tc) {
            // Match found
            score += 10;

            // Bonus for consecutive matches
            if (consecutive > 0) {
                score += consecutive * 5;
            }
            consecutive++;

            // Bonus for word start match
            if (ti == 0 || text[ti - 1] == ' ' || text[ti - 1] == '_' || text[ti - 1] == '-') {
                score += 15;
            }

            // Bonus for exact case match
            if (query[qi] == text[ti]) {
                score += 2;
            }

            qi++;
        } else {
            consecutive = 0;
        }
    }

    // Return 0 if not all query chars matched
    if (qi < query_len) {
        return 0;
    }

    return score;
}

void palette_filter(PaletteState *palette)
{
    palette->filtered_count = 0;

    // Store scores for sorting
    int scores[PALETTE_MAX_COMMANDS];

    for (int i = 0; i < palette->command_count; i++) {
        PaletteCommand *cmd = &palette->commands[i];

        // Calculate score against name, category, and ID
        int name_score = palette_fuzzy_score(palette->input, cmd->name);
        int cat_score = palette_fuzzy_score(palette->input, cmd->category);
        int id_score = palette_fuzzy_score(palette->input, cmd->id);

        int best_score = name_score;
        if (cat_score > best_score) best_score = cat_score;
        if (id_score > best_score) best_score = id_score;

        if (best_score > 0) {
            // Boost recent commands
            for (int r = 0; r < palette->recent_count; r++) {
                if (strcmp(palette->recent[r], cmd->id) == 0) {
                    best_score += 50 * (palette->recent_count - r);
                    break;
                }
            }

            palette->filtered[palette->filtered_count] = i;
            scores[palette->filtered_count] = best_score;
            palette->filtered_count++;
        }
    }

    // Sort by score (simple bubble sort since count is small)
    for (int i = 0; i < palette->filtered_count - 1; i++) {
        for (int j = i + 1; j < palette->filtered_count; j++) {
            if (scores[j] > scores[i]) {
                int tmp = palette->filtered[i];
                palette->filtered[i] = palette->filtered[j];
                palette->filtered[j] = tmp;

                int tmp_score = scores[i];
                scores[i] = scores[j];
                scores[j] = tmp_score;
            }
        }
    }

    // Reset selection if out of range
    if (palette->selected_index >= palette->filtered_count) {
        palette->selected_index = palette->filtered_count > 0 ? palette->filtered_count - 1 : 0;
    }
}

static void add_to_recent(PaletteState *palette, const char *id)
{
    // Remove if already present
    for (int i = 0; i < palette->recent_count; i++) {
        if (strcmp(palette->recent[i], id) == 0) {
            // Shift down
            for (int j = i; j > 0; j--) {
                strcpy(palette->recent[j], palette->recent[j - 1]);
            }
            strcpy(palette->recent[0], id);
            return;
        }
    }

    // Add to front
    if (palette->recent_count < PALETTE_MAX_RECENT) {
        palette->recent_count++;
    }
    for (int i = palette->recent_count - 1; i > 0; i--) {
        strcpy(palette->recent[i], palette->recent[i - 1]);
    }
    strncpy(palette->recent[0], id, sizeof(palette->recent[0]) - 1);
}

void palette_execute_selected(struct App *app)
{
    PaletteState *palette = &app->palette;

    if (palette->filtered_count == 0) {
        return;
    }

    int cmd_idx = palette->filtered[palette->selected_index];
    PaletteCommand *cmd = &palette->commands[cmd_idx];

    // Check if selection is required
    if (cmd->requires_selection && app->selection.count == 0 && app->selected_index < 0) {
        // Could show error message here
        return;
    }

    // Add to recent
    add_to_recent(palette, cmd->id);

    // Execute
    if (cmd->callback) {
        cmd->callback(app);
    }

    palette_hide(palette);
}

bool palette_execute(struct App *app, const char *command_id)
{
    PaletteState *palette = &app->palette;

    for (int i = 0; i < palette->command_count; i++) {
        if (strcmp(palette->commands[i].id, command_id) == 0) {
            PaletteCommand *cmd = &palette->commands[i];

            if (cmd->requires_selection && app->selection.count == 0 && app->selected_index < 0) {
                return false;
            }

            add_to_recent(palette, cmd->id);

            if (cmd->callback) {
                cmd->callback(app);
            }
            return true;
        }
    }
    return false;
}

void palette_handle_input(struct App *app)
{
    PaletteState *palette = &app->palette;

    if (!palette->visible) {
        return;
    }

    // Escape to close
    if (IsKeyPressed(KEY_ESCAPE)) {
        palette_hide(palette);
        return;
    }

    // Enter to execute
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        palette_execute_selected(app);
        return;
    }

    // Arrow keys for navigation
    if (IsKeyPressed(KEY_UP)) {
        palette->selected_index--;
        if (palette->selected_index < 0) {
            palette->selected_index = palette->filtered_count > 0 ? palette->filtered_count - 1 : 0;
        }
        // Adjust scroll
        if (palette->selected_index < palette->scroll_offset) {
            palette->scroll_offset = palette->selected_index;
        }
    }

    if (IsKeyPressed(KEY_DOWN)) {
        palette->selected_index++;
        if (palette->selected_index >= palette->filtered_count) {
            palette->selected_index = 0;
        }
        // Adjust scroll
        if (palette->selected_index >= palette->scroll_offset + PALETTE_VISIBLE_ITEMS) {
            palette->scroll_offset = palette->selected_index - PALETTE_VISIBLE_ITEMS + 1;
        }
    }

    // Text input
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key <= 126) {
            int len = (int)strlen(palette->input);
            if (len < PALETTE_INPUT_MAX - 1) {
                palette->input[len] = (char)key;
                palette->input[len + 1] = '\0';
                palette->input_cursor = len + 1;
                palette_filter(palette);
                palette->selected_index = 0;
                palette->scroll_offset = 0;
            }
        }
        key = GetCharPressed();
    }

    // Backspace
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        int len = (int)strlen(palette->input);
        if (len > 0) {
            palette->input[len - 1] = '\0';
            palette->input_cursor = len - 1;
            palette_filter(palette);
            palette->selected_index = 0;
            palette->scroll_offset = 0;
        }
    }
}

void palette_draw(struct App *app)
{
    PaletteState *palette = &app->palette;

    if (palette->animation_progress <= 0.0f) {
        return;
    }

    Theme *theme = theme_get_current();

    // Draw overlay
    DrawRectangle(0, 0, app->width, app->height, Fade(BLACK, 0.5f * palette->animation_progress));

    // Calculate palette position (centered)
    int p_width = PALETTE_WIDTH;
    int p_height = PALETTE_HEIGHT;
    int p_x = (app->width - p_width) / 2;
    int p_y = (int)(100 * palette->animation_progress);

    // Animate from top
    p_y = (int)(p_y - (1.0f - palette->animation_progress) * 50);

    // Draw palette background
    DrawRectangle(p_x, p_y, p_width, p_height, theme->sidebar);
    DrawRectangleLines(p_x, p_y, p_width, p_height, theme->border);

    // Draw input field
    DrawRectangle(p_x + PALETTE_PADDING, p_y + PALETTE_PADDING,
                  p_width - PALETTE_PADDING * 2, PALETTE_INPUT_HEIGHT, theme->background);
    DrawRectangleLines(p_x + PALETTE_PADDING, p_y + PALETTE_PADDING,
                       p_width - PALETTE_PADDING * 2, PALETTE_INPUT_HEIGHT, theme->accent);

    // Draw input text or placeholder
    int text_y = p_y + PALETTE_PADDING + (PALETTE_INPUT_HEIGHT - 16) / 2;
    if (palette->input[0] != '\0') {
        DrawTextCustom(palette->input, p_x + PALETTE_PADDING + 10, text_y, 16, theme->textPrimary);
    } else {
        DrawTextCustom("Type a command...", p_x + PALETTE_PADDING + 10, text_y, 16, theme->textSecondary);
    }

    // Draw cursor
    int cursor_x = p_x + PALETTE_PADDING + 10 + MeasureTextCustom(palette->input, 16);
    if ((int)(GetTime() * 2) % 2 == 0) {
        DrawLine(cursor_x, text_y, cursor_x, text_y + 16, theme->textPrimary);
    }

    // Draw command list
    int list_y = p_y + PALETTE_PADDING + PALETTE_INPUT_HEIGHT + PALETTE_PADDING;
    int list_height = p_height - PALETTE_PADDING * 3 - PALETTE_INPUT_HEIGHT;
    int visible_items = list_height / PALETTE_ITEM_HEIGHT;

    for (int i = palette->scroll_offset; i < palette->filtered_count && i < palette->scroll_offset + visible_items; i++) {
        int cmd_idx = palette->filtered[i];
        PaletteCommand *cmd = &palette->commands[cmd_idx];

        int item_y = list_y + (i - palette->scroll_offset) * PALETTE_ITEM_HEIGHT;

        // Highlight selected item
        if (i == palette->selected_index) {
            DrawRectangle(p_x + PALETTE_PADDING, item_y,
                          p_width - PALETTE_PADDING * 2, PALETTE_ITEM_HEIGHT, theme->selection);
        }

        // Draw category
        DrawTextCustom(cmd->category, p_x + PALETTE_PADDING + 10, item_y + (PALETTE_ITEM_HEIGHT - 14) / 2, 12, theme->textSecondary);

        // Draw command name
        int name_x = p_x + PALETTE_PADDING + 80;
        DrawTextCustom(cmd->name, name_x, item_y + (PALETTE_ITEM_HEIGHT - 14) / 2, 14,
                 i == palette->selected_index ? theme->accent : theme->textPrimary);

        // Draw shortcut
        if (cmd->shortcut) {
            int shortcut_w = MeasureTextCustom(cmd->shortcut, 12);
            DrawTextCustom(cmd->shortcut, p_x + p_width - PALETTE_PADDING - 10 - shortcut_w,
                     item_y + (PALETTE_ITEM_HEIGHT - 12) / 2, 12, theme->textSecondary);
        }
    }

    // Draw scrollbar if needed
    if (palette->filtered_count > visible_items) {
        int scroll_height = (visible_items * list_height) / palette->filtered_count;
        int scroll_y = list_y + (palette->scroll_offset * list_height) / palette->filtered_count;
        DrawRectangle(p_x + p_width - PALETTE_PADDING - 4, scroll_y, 4, scroll_height, theme->border);
    }

    // Draw hint at bottom
    const char *hint = "Press Enter to execute, Escape to close";
    int hint_w = MeasureTextCustom(hint, 12);
    DrawTextCustom(hint, p_x + (p_width - hint_w) / 2, p_y + p_height - PALETTE_PADDING - 12, 12, theme->textSecondary);
}

// ============================================================================
// Built-in command implementations
// ============================================================================

static void cmd_new_file(struct App *app)
{
    // Start rename mode for new file creation
    // This would typically trigger a dialog or inline rename
    (void)app;
}

static void cmd_new_folder(struct App *app)
{
    file_create_directory(app->directory.current_path, "New Folder");
    directory_read(&app->directory, app->directory.current_path);
}

static void cmd_open(struct App *app)
{
    if (app->selected_index >= 0 && app->selected_index < app->directory.count) {
        FileEntry *entry = &app->directory.entries[app->selected_index];
        if (entry->is_directory) {
            directory_enter(&app->directory, app->selected_index);
        }
    }
}

// Helper to collect selected paths and perform clipboard operation
static void copy_or_cut_selected(struct App *app, bool is_cut)
{
    const char *paths[MAX_SELECTION];
    int count = 0;

    if (app->selection.count > 0) {
        for (int i = 0; i < app->selection.count && i < MAX_SELECTION; i++) {
            int idx = app->selection.indices[i];
            if (idx >= 0 && idx < app->directory.count) {
                paths[count++] = app->directory.entries[idx].path;
            }
        }
    } else if (app->selected_index >= 0 && app->selected_index < app->directory.count) {
        paths[count++] = app->directory.entries[app->selected_index].path;
    }

    if (count > 0) {
        if (is_cut) {
            clipboard_cut(&app->clipboard, paths, count);
        } else {
            clipboard_copy(&app->clipboard, paths, count);
        }
    }
}

static void cmd_copy(struct App *app) { copy_or_cut_selected(app, false); }
static void cmd_cut(struct App *app) { copy_or_cut_selected(app, true); }

static void cmd_paste(struct App *app)
{
    if (clipboard_has_items(&app->clipboard)) {
        clipboard_paste(&app->clipboard, app->directory.current_path);
        directory_read(&app->directory, app->directory.current_path);
    }
}

static void cmd_delete(struct App *app)
{
    if (app->selection.count > 0) {
        for (int i = 0; i < app->selection.count; i++) {
            int idx = app->selection.indices[i];
            if (idx >= 0 && idx < app->directory.count) {
                file_delete(app->directory.entries[idx].path);
            }
        }
        selection_clear(&app->selection);
    } else if (app->selected_index >= 0 && app->selected_index < app->directory.count) {
        file_delete(app->directory.entries[app->selected_index].path);
    }
    directory_read(&app->directory, app->directory.current_path);
}

static void cmd_rename(struct App *app)
{
    if (app->selected_index >= 0 && app->selected_index < app->directory.count) {
        app->rename_mode = true;
        app->rename_index = app->selected_index;
        strncpy(app->rename_buffer, app->directory.entries[app->selected_index].name, NAME_MAX_LEN - 1);
        app->rename_cursor = (int)strlen(app->rename_buffer);
    }
}

static void cmd_duplicate(struct App *app)
{
    if (app->selected_index >= 0 && app->selected_index < app->directory.count) {
        file_duplicate(app->directory.entries[app->selected_index].path);
        directory_read(&app->directory, app->directory.current_path);
    }
}

static void cmd_select_all(struct App *app)
{
    selection_select_all(app);
}

static void cmd_view_list(struct App *app)
{
    app->view_mode = VIEW_LIST;
}

static void cmd_view_grid(struct App *app)
{
    app->view_mode = VIEW_GRID;
}

static void cmd_view_columns(struct App *app)
{
    app->view_mode = VIEW_COLUMN;
}

static void cmd_toggle_hidden(struct App *app)
{
    directory_toggle_hidden(&app->directory);
}

static void cmd_toggle_preview(struct App *app)
{
    app->preview.visible = !app->preview.visible;
}

static void cmd_go_back(struct App *app)
{
    const char *path = history_back(&app->history);
    if (path) {
        directory_read(&app->directory, path);
    }
}

static void cmd_go_forward(struct App *app)
{
    const char *path = history_forward(&app->history);
    if (path) {
        directory_read(&app->directory, path);
    }
}

static void cmd_go_parent(struct App *app)
{
    directory_go_parent(&app->directory);
    history_push(&app->history, app->directory.current_path);
}

static void cmd_go_home(struct App *app)
{
    const char *home = getenv("HOME");
    if (home) {
        directory_read(&app->directory, home);
        history_push(&app->history, app->directory.current_path);
    }
}

static void cmd_refresh(struct App *app)
{
    directory_read(&app->directory, app->directory.current_path);
}

static void cmd_new_tab(struct App *app)
{
    tabs_new(&app->tabs, app->directory.current_path);
}

static void cmd_close_tab(struct App *app)
{
    tabs_close(&app->tabs, app->tabs.current);
}

static void cmd_toggle_sidebar(struct App *app)
{
    app->sidebar.collapsed = !app->sidebar.collapsed;
}

static void cmd_toggle_fullscreen(struct App *app)
{
    app->fullscreen = !app->fullscreen;
    ToggleFullscreen();
}

static void cmd_toggle_theme(struct App *app)
{
    (void)app;
    theme_toggle();
}

static void cmd_show_queue(struct App *app)
{
    queue_panel_toggle(&app->queue_panel);
}

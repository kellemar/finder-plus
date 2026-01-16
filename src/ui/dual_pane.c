#include "dual_pane.h"
#include "tabs.h"
#include "breadcrumb.h"
#include "../app.h"
#include "../core/operations.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "raylib.h"

// Use constants from headers to avoid linker issues in test builds
#define DUAL_PANE_TAB_HEIGHT TAB_HEIGHT
#define DUAL_PANE_BREADCRUMB_HEIGHT BREADCRUMB_HEIGHT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Layout constants
#define PANE_DIVIDER_WIDTH 4
#define PANE_HEADER_HEIGHT 28
#define PANE_ROW_HEIGHT 24
#define PANE_PADDING 8
#define PANE_ICON_WIDTH 24

// Forward declarations
static void draw_pane(struct App *app, PaneState *pane, int x, int y, int width, int height, bool is_active, CompareResult *compare_results, int compare_count);
static void pane_ensure_visible(PaneState *pane, int visible_rows);

// Initialize a single pane
static void pane_init(PaneState *pane)
{
    directory_state_init(&pane->directory);
    pane->selected_index = 0;
    pane->scroll_offset = 0;
    pane->current_path[0] = '\0';
}

// Free a single pane
static void pane_free(PaneState *pane)
{
    directory_state_free(&pane->directory);
}

void dual_pane_init(DualPaneState *state)
{
    state->enabled = false;
    pane_init(&state->left);
    pane_init(&state->right);
    state->active_pane = PANE_LEFT;
    state->sync_scroll = false;
    state->compare_mode = false;
    state->compare_results_left = NULL;
    state->compare_results_right = NULL;
    state->compare_count_left = 0;
    state->compare_count_right = 0;
}

void dual_pane_free(DualPaneState *state)
{
    pane_free(&state->left);
    pane_free(&state->right);

    if (state->compare_results_left) {
        free(state->compare_results_left);
        state->compare_results_left = NULL;
    }
    if (state->compare_results_right) {
        free(state->compare_results_right);
        state->compare_results_right = NULL;
    }
}

void dual_pane_toggle(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    state->enabled = !state->enabled;

    if (state->enabled) {
        // When enabling, sync left pane from app state
        dual_pane_sync_from_app(app);

        // Initialize right pane with same directory as left
        if (state->right.current_path[0] == '\0') {
            strncpy(state->right.current_path, state->left.current_path, PATH_MAX_LEN - 1);
            state->right.current_path[PATH_MAX_LEN - 1] = '\0';
            directory_read(&state->right.directory, state->right.current_path);
            state->right.selected_index = 0;
            state->right.scroll_offset = 0;
        }
    } else {
        // When disabling, sync back to app
        dual_pane_sync_to_app(app);
    }
}

bool dual_pane_is_enabled(DualPaneState *state)
{
    return state->enabled;
}

void dual_pane_switch_pane(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    if (!state->enabled) return;

    state->active_pane = (state->active_pane == PANE_LEFT) ? PANE_RIGHT : PANE_LEFT;
}

PaneId dual_pane_get_active(DualPaneState *state)
{
    return state->active_pane;
}

PaneState* dual_pane_get_active_pane(DualPaneState *state)
{
    return (state->active_pane == PANE_LEFT) ? &state->left : &state->right;
}

PaneState* dual_pane_get_inactive_pane(DualPaneState *state)
{
    return (state->active_pane == PANE_LEFT) ? &state->right : &state->left;
}

bool dual_pane_navigate_to(struct App *app, const char *path)
{
    DualPaneState *state = &app->dual_pane;
    PaneState *pane = dual_pane_get_active_pane(state);

    if (directory_read(&pane->directory, path)) {
        strncpy(pane->current_path, path, PATH_MAX_LEN - 1);
        pane->current_path[PATH_MAX_LEN - 1] = '\0';
        pane->selected_index = 0;
        pane->scroll_offset = 0;

        // Clear comparison results when navigating
        if (state->compare_mode) {
            state->compare_mode = false;
        }

        return true;
    }
    return false;
}

bool dual_pane_enter_directory(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    PaneState *pane = dual_pane_get_active_pane(state);

    if (pane->directory.count == 0) return false;

    FileEntry *entry = &pane->directory.entries[pane->selected_index];
    if (!entry->is_directory) return false;

    return dual_pane_navigate_to(app, entry->path);
}

bool dual_pane_go_parent(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    PaneState *pane = dual_pane_get_active_pane(state);

    // Get parent path
    char parent[PATH_MAX_LEN];
    strncpy(parent, pane->current_path, PATH_MAX_LEN - 1);
    parent[PATH_MAX_LEN - 1] = '\0';

    char *last_slash = strrchr(parent, '/');
    if (last_slash == NULL || last_slash == parent) {
        // Already at root
        if (strcmp(parent, "/") != 0) {
            return dual_pane_navigate_to(app, "/");
        }
        return false;
    }

    *last_slash = '\0';
    return dual_pane_navigate_to(app, parent);
}

void dual_pane_copy_to_other(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    PaneState *active = dual_pane_get_active_pane(state);
    PaneState *other = dual_pane_get_inactive_pane(state);

    if (active->directory.count == 0) return;

    FileEntry *entry = &active->directory.entries[active->selected_index];
    file_copy(entry->path, other->current_path);

    // Refresh the other pane
    directory_read(&other->directory, other->current_path);
}

void dual_pane_move_to_other(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    PaneState *active = dual_pane_get_active_pane(state);
    PaneState *other = dual_pane_get_inactive_pane(state);

    if (active->directory.count == 0) return;

    FileEntry *entry = &active->directory.entries[active->selected_index];
    file_move(entry->path, other->current_path);

    // Refresh both panes
    directory_read(&active->directory, active->current_path);
    directory_read(&other->directory, other->current_path);

    // Adjust selection if needed
    if (active->selected_index >= active->directory.count) {
        active->selected_index = active->directory.count > 0 ? active->directory.count - 1 : 0;
    }
}

void dual_pane_toggle_sync_scroll(DualPaneState *state)
{
    state->sync_scroll = !state->sync_scroll;
}

void dual_pane_toggle_compare(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    state->compare_mode = !state->compare_mode;

    if (state->compare_mode) {
        dual_pane_run_comparison(app);
    }
}

// Compare two files by content (using file size and modification time for quick comparison)
static CompareResult compare_files(const char *path1, const char *path2)
{
    struct stat st1, st2;

    if (stat(path1, &st1) != 0) return COMPARE_RIGHT_ONLY;
    if (stat(path2, &st2) != 0) return COMPARE_LEFT_ONLY;

    if (S_ISDIR(st1.st_mode) || S_ISDIR(st2.st_mode)) {
        return COMPARE_DIR;
    }

    // Compare by size and mtime (quick comparison)
    if (st1.st_size == st2.st_size && st1.st_mtime == st2.st_mtime) {
        return COMPARE_SAME;
    }

    return COMPARE_DIFFERENT;
}

void dual_pane_run_comparison(struct App *app)
{
    DualPaneState *state = &app->dual_pane;

    // Free previous comparison results
    if (state->compare_results_left) {
        free(state->compare_results_left);
        state->compare_results_left = NULL;
    }
    if (state->compare_results_right) {
        free(state->compare_results_right);
        state->compare_results_right = NULL;
    }

    // Allocate new results
    if (state->left.directory.count > 0) {
        state->compare_results_left = malloc(state->left.directory.count * sizeof(CompareResult));
        if (!state->compare_results_left) {
            state->compare_count_left = 0;
            return;
        }
        state->compare_count_left = state->left.directory.count;
    }
    if (state->right.directory.count > 0) {
        state->compare_results_right = malloc(state->right.directory.count * sizeof(CompareResult));
        if (!state->compare_results_right) {
            // Clean up left allocation if right fails
            if (state->compare_results_left) {
                free(state->compare_results_left);
                state->compare_results_left = NULL;
                state->compare_count_left = 0;
            }
            state->compare_count_right = 0;
            return;
        }
        state->compare_count_right = state->right.directory.count;
    }

    // Compare left pane entries
    for (int i = 0; i < state->left.directory.count; i++) {
        FileEntry *left_entry = &state->left.directory.entries[i];

        // Look for matching file in right pane
        bool found = false;
        for (int j = 0; j < state->right.directory.count; j++) {
            FileEntry *right_entry = &state->right.directory.entries[j];
            if (strcmp(left_entry->name, right_entry->name) == 0) {
                state->compare_results_left[i] = compare_files(left_entry->path, right_entry->path);
                found = true;
                break;
            }
        }

        if (!found) {
            state->compare_results_left[i] = left_entry->is_directory ? COMPARE_DIR : COMPARE_LEFT_ONLY;
        }
    }

    // Compare right pane entries
    for (int i = 0; i < state->right.directory.count; i++) {
        FileEntry *right_entry = &state->right.directory.entries[i];

        // Look for matching file in left pane
        bool found = false;
        for (int j = 0; j < state->left.directory.count; j++) {
            FileEntry *left_entry = &state->left.directory.entries[j];
            if (strcmp(right_entry->name, left_entry->name) == 0) {
                state->compare_results_right[i] = compare_files(left_entry->path, right_entry->path);
                found = true;
                break;
            }
        }

        if (!found) {
            state->compare_results_right[i] = right_entry->is_directory ? COMPARE_DIR : COMPARE_RIGHT_ONLY;
        }
    }
}

CompareResult dual_pane_get_compare_result(DualPaneState *state, PaneId pane, int index)
{
    if (pane == PANE_LEFT) {
        if (index >= 0 && index < state->compare_count_left && state->compare_results_left) {
            return state->compare_results_left[index];
        }
    } else {
        if (index >= 0 && index < state->compare_count_right && state->compare_results_right) {
            return state->compare_results_right[index];
        }
    }
    return COMPARE_SAME;
}

void dual_pane_handle_input(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    if (!state->enabled) return;

    PaneState *pane = dual_pane_get_active_pane(state);
    bool cmd_down = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    bool shift_down = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    // Tab - switch panes
    if (IsKeyPressed(KEY_TAB) && !cmd_down) {
        dual_pane_switch_pane(app);
        return;
    }

    // Cmd+Tab - also switch panes (alternative)
    if (cmd_down && IsKeyPressed(KEY_TAB)) {
        dual_pane_switch_pane(app);
        return;
    }

    // F5 or Cmd+C in dual mode - copy to other pane
    if (IsKeyPressed(KEY_F5) || (cmd_down && IsKeyPressed(KEY_C))) {
        dual_pane_copy_to_other(app);
        return;
    }

    // F6 or Cmd+X in dual mode - move to other pane
    if (IsKeyPressed(KEY_F6) || (cmd_down && IsKeyPressed(KEY_X))) {
        dual_pane_move_to_other(app);
        return;
    }

    // Cmd+= - toggle compare mode
    if (cmd_down && IsKeyPressed(KEY_EQUAL)) {
        dual_pane_toggle_compare(app);
        return;
    }

    // Cmd+Shift+S - toggle sync scroll
    if (cmd_down && shift_down && IsKeyPressed(KEY_S)) {
        dual_pane_toggle_sync_scroll(state);
        return;
    }

    // Calculate visible rows for scrolling
    int content_offset = DUAL_PANE_TAB_HEIGHT + DUAL_PANE_BREADCRUMB_HEIGHT;
    int pane_height = app->height - STATUSBAR_HEIGHT - content_offset - PANE_HEADER_HEIGHT;
    int visible_rows = pane_height / PANE_ROW_HEIGHT;

    // Navigation: j/Down - move down
    if (IsKeyPressed(KEY_J) || IsKeyPressed(KEY_DOWN)) {
        if (pane->selected_index < pane->directory.count - 1) {
            pane->selected_index++;
            pane_ensure_visible(pane, visible_rows);

            // Sync scroll if enabled
            if (state->sync_scroll) {
                PaneState *other = dual_pane_get_inactive_pane(state);
                other->scroll_offset = pane->scroll_offset;
            }
        }
    }

    // Navigation: k/Up - move up
    if (IsKeyPressed(KEY_K) || IsKeyPressed(KEY_UP)) {
        if (pane->selected_index > 0) {
            pane->selected_index--;
            pane_ensure_visible(pane, visible_rows);

            // Sync scroll if enabled
            if (state->sync_scroll) {
                PaneState *other = dual_pane_get_inactive_pane(state);
                other->scroll_offset = pane->scroll_offset;
            }
        }
    }

    // Navigation: l/Right/Enter - enter directory
    if (IsKeyPressed(KEY_L) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_ENTER)) {
        dual_pane_enter_directory(app);
    }

    // Navigation: h/Left/Backspace - go to parent
    if (IsKeyPressed(KEY_H) || IsKeyPressed(KEY_LEFT) ||
        (!cmd_down && IsKeyPressed(KEY_BACKSPACE))) {
        dual_pane_go_parent(app);
    }

    // gg - go to top
    // G (shift+g) - go to bottom
    if (IsKeyPressed(KEY_G)) {
        if (shift_down) {
            pane->selected_index = pane->directory.count > 0 ? pane->directory.count - 1 : 0;
        } else if (app->g_pressed) {
            pane->selected_index = 0;
            app->g_pressed = false;
            app->g_timer = 0.0f;
        } else {
            app->g_pressed = true;
            app->g_timer = 0.0f;
        }
        pane_ensure_visible(pane, visible_rows);
    }
}

static void pane_ensure_visible(PaneState *pane, int visible_rows)
{
    if (pane->selected_index < pane->scroll_offset) {
        pane->scroll_offset = pane->selected_index;
    }

    if (pane->selected_index >= pane->scroll_offset + visible_rows) {
        pane->scroll_offset = pane->selected_index - visible_rows + 1;
    }

    int max_scroll = pane->directory.count - visible_rows;
    if (max_scroll < 0) max_scroll = 0;

    if (pane->scroll_offset > max_scroll) {
        pane->scroll_offset = max_scroll;
    }
    if (pane->scroll_offset < 0) {
        pane->scroll_offset = 0;
    }
}

// Get color for comparison result
static Color get_compare_color(CompareResult result)
{
    switch (result) {
        case COMPARE_SAME:       return g_theme.textSecondary;
        case COMPARE_DIFFERENT:  return g_theme.warning;
        case COMPARE_LEFT_ONLY:  return g_theme.gitUntracked;
        case COMPARE_RIGHT_ONLY: return g_theme.gitUntracked;
        case COMPARE_DIR:        return g_theme.folder;
        default:                 return g_theme.textPrimary;
    }
}

static void draw_pane(struct App *app, PaneState *pane, int x, int y, int width, int height, bool is_active, CompareResult *compare_results, int compare_count)
{
    DualPaneState *state = &app->dual_pane;

    // Draw pane background
    Color bg = is_active ? g_theme.background : Fade(g_theme.sidebar, 0.5f);
    DrawRectangle(x, y, width, height, bg);

    // Draw pane header with path
    Color header_bg = is_active ? g_theme.selection : g_theme.sidebar;
    DrawRectangle(x, y, width, PANE_HEADER_HEIGHT, header_bg);

    // Truncate path to fit
    char display_path[128];
    int max_chars = (width - PANE_PADDING * 2) / 8;
    if (max_chars < 1) max_chars = 1;

    int path_len = (int)strlen(pane->current_path);
    if (path_len > max_chars && max_chars > 6) {
        // Need room for "..." plus at least 3 chars from path
        strncpy(display_path, "...", 4);
        int remaining = max_chars - 3;
        if (remaining > 0 && remaining < path_len) {
            strncat(display_path, pane->current_path + path_len - remaining, remaining);
        }
    } else {
        strncpy(display_path, pane->current_path, sizeof(display_path) - 1);
        display_path[sizeof(display_path) - 1] = '\0';
    }

    Color path_color = is_active ? g_theme.textPrimary : g_theme.textSecondary;
    DrawTextCustom(display_path, x + PANE_PADDING, y + (PANE_HEADER_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, path_color);

    // Draw active indicator
    if (is_active) {
        DrawRectangle(x, y, 3, PANE_HEADER_HEIGHT, g_theme.accent);
    }

    // Draw sync scroll indicator
    if (state->sync_scroll) {
        DrawTextCustom("S", x + width - 20, y + (PANE_HEADER_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, g_theme.accent);
    }

    // Content area
    int content_y = y + PANE_HEADER_HEIGHT;
    int content_height = height - PANE_HEADER_HEIGHT;
    int visible_rows = content_height / PANE_ROW_HEIGHT;

    // Draw border
    DrawLine(x + width - 1, y, x + width - 1, y + height, g_theme.border);

    // Show error message if any
    if (pane->directory.error_message[0] != '\0') {
        DrawTextCustom(pane->directory.error_message, x + PANE_PADDING, content_y + PANE_PADDING, FONT_SIZE_SMALL, g_theme.error);
        return;
    }

    // Show empty directory message
    if (pane->directory.count == 0) {
        DrawTextCustom("Empty folder", x + PANE_PADDING, content_y + PANE_PADDING, FONT_SIZE_SMALL, g_theme.textSecondary);
        return;
    }

    // Draw file entries
    for (int i = 0; i < visible_rows && (pane->scroll_offset + i) < pane->directory.count; i++) {
        int entry_index = pane->scroll_offset + i;
        FileEntry *entry = &pane->directory.entries[entry_index];

        int row_y = content_y + i * PANE_ROW_HEIGHT;
        bool is_selected = (entry_index == pane->selected_index);

        // Selection background
        if (is_selected && is_active) {
            DrawRectangle(x, row_y, width - 1, PANE_ROW_HEIGHT, g_theme.selection);
        } else if (is_selected) {
            DrawRectangle(x, row_y, width - 1, PANE_ROW_HEIGHT, Fade(g_theme.selection, 0.4f));
        }

        int text_x = x + PANE_PADDING;

        // Icon
        Color icon_color = entry->is_directory ? g_theme.folder : g_theme.file;
        const char *icon = entry->is_directory ? "[D]" : "[F]";
        DrawTextCustom(icon, text_x, row_y + (PANE_ROW_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, icon_color);
        text_x += PANE_ICON_WIDTH;

        // Name
        char display_name[64];
        int max_name_chars = (width - PANE_PADDING * 2 - PANE_ICON_WIDTH - 40) / 7;
        if ((int)strlen(entry->name) > max_name_chars && max_name_chars > 3) {
            strncpy(display_name, entry->name, max_name_chars - 2);
            display_name[max_name_chars - 2] = '\0';
            strcat(display_name, "..");
        } else {
            strncpy(display_name, entry->name, sizeof(display_name) - 1);
            display_name[sizeof(display_name) - 1] = '\0';
        }

        // Color based on compare result if in compare mode
        Color name_color = entry->is_hidden ? g_theme.textSecondary : g_theme.textPrimary;
        if (state->compare_mode && compare_results && entry_index < compare_count) {
            name_color = get_compare_color(compare_results[entry_index]);
        }

        DrawTextCustom(display_name, text_x, row_y + (PANE_ROW_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, name_color);

        // Size for files
        if (!entry->is_directory) {
            char size_str[16];
            format_file_size(entry->size, size_str, sizeof(size_str));
            int size_width = MeasureTextCustom(size_str, FONT_SIZE_SMALL);
            DrawTextCustom(size_str, x + width - size_width - PANE_PADDING - 4, row_y + (PANE_ROW_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, g_theme.textSecondary);
        }
    }

    // Draw scrollbar if needed
    if (pane->directory.count > visible_rows) {
        int scrollbar_x = x + width - 6;
        int scrollbar_height = content_height;
        int thumb_height = (visible_rows * scrollbar_height) / pane->directory.count;
        if (thumb_height < 20) thumb_height = 20;

        int thumb_y = content_y + (pane->scroll_offset * (scrollbar_height - thumb_height)) / (pane->directory.count - visible_rows);

        DrawRectangle(scrollbar_x, content_y, 4, scrollbar_height, Fade(g_theme.sidebar, 0.5f));
        DrawRectangle(scrollbar_x, thumb_y, 4, thumb_height, g_theme.selection);
    }
}

void dual_pane_draw(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    if (!state->enabled) return;

    // Calculate layout
    int sidebar_width = app->sidebar.collapsed ? 0 : app->sidebar.width;
    int content_x = sidebar_width;
    int content_width = app->width - sidebar_width;
    int content_offset = DUAL_PANE_TAB_HEIGHT + DUAL_PANE_BREADCRUMB_HEIGHT;
    int content_height = app->height - STATUSBAR_HEIGHT - content_offset;

    int pane_width = (content_width - PANE_DIVIDER_WIDTH) / 2;

    // Draw left pane
    draw_pane(app, &state->left, content_x, content_offset, pane_width, content_height,
              state->active_pane == PANE_LEFT, state->compare_results_left, state->compare_count_left);

    // Draw divider
    int divider_x = content_x + pane_width;
    DrawRectangle(divider_x, content_offset, PANE_DIVIDER_WIDTH, content_height, g_theme.border);

    // Draw right pane
    draw_pane(app, &state->right, divider_x + PANE_DIVIDER_WIDTH, content_offset, pane_width, content_height,
              state->active_pane == PANE_RIGHT, state->compare_results_right, state->compare_count_right);

    // Draw comparison legend if in compare mode
    if (state->compare_mode) {
        int legend_y = content_offset + content_height - 24;
        DrawRectangle(content_x, legend_y, content_width, 24, Fade(g_theme.background, 0.9f));

        int legend_x = content_x + PANE_PADDING;
        DrawTextCustom("Compare:", legend_x, legend_y + 4, FONT_SIZE_SMALL, g_theme.textSecondary);
        legend_x += 60;

        DrawRectangle(legend_x, legend_y + 6, 10, 10, g_theme.textSecondary);
        DrawTextCustom("Same", legend_x + 14, legend_y + 4, FONT_SIZE_SMALL, g_theme.textSecondary);
        legend_x += 60;

        DrawRectangle(legend_x, legend_y + 6, 10, 10, g_theme.warning);
        DrawTextCustom("Different", legend_x + 14, legend_y + 4, FONT_SIZE_SMALL, g_theme.warning);
        legend_x += 80;

        DrawRectangle(legend_x, legend_y + 6, 10, 10, g_theme.gitUntracked);
        DrawTextCustom("Unique", legend_x + 14, legend_y + 4, FONT_SIZE_SMALL, g_theme.gitUntracked);
    }
}

void dual_pane_sync_to_app(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    PaneState *active = dual_pane_get_active_pane(state);

    // Copy active pane state to app
    strncpy(app->directory.current_path, active->current_path, PATH_MAX_LEN - 1);
    app->selected_index = active->selected_index;
    app->scroll_offset = active->scroll_offset;

    // Refresh app directory
    directory_read(&app->directory, app->directory.current_path);
}

void dual_pane_sync_from_app(struct App *app)
{
    DualPaneState *state = &app->dual_pane;
    PaneState *active = dual_pane_get_active_pane(state);

    // Copy app state to active pane
    strncpy(active->current_path, app->directory.current_path, PATH_MAX_LEN - 1);
    active->current_path[PATH_MAX_LEN - 1] = '\0';
    directory_read(&active->directory, active->current_path);
    active->selected_index = app->selected_index;
    active->scroll_offset = app->scroll_offset;
}

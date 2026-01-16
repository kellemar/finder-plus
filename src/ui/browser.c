#include "browser.h"
#include "sidebar.h"
#include "tabs.h"
#include "preview.h"
#include "breadcrumb.h"
#include "file_view_modal.h"
#include "../app.h"
#include "../core/filesystem.h"
#include "../core/search.h"
#include "../core/git.h"
#include "../core/operations.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

// Column widths for list view
#define NAME_COL_WIDTH 400
#define SIZE_COL_WIDTH 100
#define DATE_COL_WIDTH 120

// Grid view constants
#define GRID_ITEM_WIDTH 100
#define GRID_ITEM_HEIGHT 90
#define GRID_ICON_SIZE 48
#define GRID_TEXT_HEIGHT 28

// Column view constants
#define COLUMN_VIEW_WIDTH 220
#define COLUMN_MIN_WIDTH 150
#define COLUMN_MAX_COLUMNS 4

// Search bar height
#define SEARCH_BAR_HEIGHT 32

// Get the y offset for browser content (tabs + search bar + breadcrumb)
static int get_content_offset_y(App *app)
{
    int offset = tabs_get_height(&app->tabs);
    if (search_is_active(&app->search)) {
        offset += SEARCH_BAR_HEIGHT;
    }
    offset += breadcrumb_get_height();
    return offset;
}

// Get content width accounting for preview panel
static int get_browser_width(App *app)
{
    int sidebar_width = app->sidebar.collapsed ? 0 : app->sidebar.width;
    int preview_width = preview_get_width(&app->preview);
    return app->width - sidebar_width - preview_width;
}

// Get icon character for file type
static const char* get_file_icon(const FileEntry *entry)
{
    if (entry->is_directory) {
        return "[D]";
    }

    // Check common extensions
    const char *ext = entry->extension;
    if (strlen(ext) == 0) {
        return "[.]";
    }

    // Code files
    if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0 ||
        strcmp(ext, "cpp") == 0 || strcmp(ext, "hpp") == 0 ||
        strcmp(ext, "py") == 0 || strcmp(ext, "js") == 0 ||
        strcmp(ext, "ts") == 0 || strcmp(ext, "rs") == 0 ||
        strcmp(ext, "go") == 0 || strcmp(ext, "java") == 0) {
        return "[C]";
    }

    // Documents
    if (strcmp(ext, "md") == 0 || strcmp(ext, "txt") == 0 ||
        strcmp(ext, "pdf") == 0 || strcmp(ext, "doc") == 0 ||
        strcmp(ext, "docx") == 0) {
        return "[T]";
    }

    // Images
    if (strcmp(ext, "png") == 0 || strcmp(ext, "jpg") == 0 ||
        strcmp(ext, "jpeg") == 0 || strcmp(ext, "gif") == 0 ||
        strcmp(ext, "svg") == 0 || strcmp(ext, "bmp") == 0) {
        return "[I]";
    }

    // Archives
    if (strcmp(ext, "zip") == 0 || strcmp(ext, "tar") == 0 ||
        strcmp(ext, "gz") == 0 || strcmp(ext, "rar") == 0 ||
        strcmp(ext, "7z") == 0) {
        return "[Z]";
    }

    // Audio/Video
    if (strcmp(ext, "mp3") == 0 || strcmp(ext, "wav") == 0 ||
        strcmp(ext, "mp4") == 0 || strcmp(ext, "avi") == 0 ||
        strcmp(ext, "mkv") == 0 || strcmp(ext, "mov") == 0) {
        return "[M]";
    }

    return "[F]";
}

// Check if an entry is selected (either cursor or multi-select)
static bool is_entry_selected(App *app, int index)
{
    if (index == app->selected_index) {
        return true;
    }
    return selection_contains(&app->selection, index);
}

// Check if an entry is in clipboard and get its operation type
static OperationType get_clipboard_operation(App *app, const char *path)
{
    if (clipboard_contains(&app->clipboard, path)) {
        return app->clipboard.operation;
    }
    return OP_NONE;
}

// Apply clipboard visual feedback to a color
// Cut items are faded (reduced alpha), Copy items are dimmed (reduced brightness)
static Color apply_clipboard_feedback(Color color, OperationType op)
{
    if (op == OP_CUT) {
        // Fade cut items significantly (50% opacity)
        return Fade(color, 0.5f);
    } else if (op == OP_COPY) {
        // Slightly dim copy items (80% brightness) with subtle tint
        return (Color){
            (unsigned char)(color.r * 0.8f),
            (unsigned char)(color.g * 0.8f),
            (unsigned char)(color.b * 0.85f),
            color.a
        };
    }
    return color;
}

// Get color for git status
static Color get_git_status_color(FileGitStatus status)
{
    switch (status) {
        case FILE_GIT_MODIFIED:  return g_theme.gitModified;
        case FILE_GIT_STAGED:    return g_theme.gitStaged;
        case FILE_GIT_UNTRACKED: return g_theme.gitUntracked;
        case FILE_GIT_CONFLICT:  return g_theme.gitConflict;
        case FILE_GIT_DELETED:   return g_theme.gitDeleted;
        case FILE_GIT_RENAMED:   return g_theme.gitStaged;
        default: return g_theme.textSecondary;
    }
}

// Get character for git status
static char get_git_status_char(FileGitStatus status)
{
    switch (status) {
        case FILE_GIT_MODIFIED:  return 'M';
        case FILE_GIT_STAGED:    return 'A';
        case FILE_GIT_UNTRACKED: return '?';
        case FILE_GIT_CONFLICT:  return 'U';
        case FILE_GIT_DELETED:   return 'D';
        case FILE_GIT_RENAMED:   return 'R';
        default: return ' ';
    }
}

// Draw list view
static void browser_draw_list(App *app)
{
    DirectoryState *dir = &app->directory;
    int content_x = sidebar_get_content_x(app);
    int content_width = get_browser_width(app);
    int content_offset = get_content_offset_y(app);
    int y = content_offset;

    // Draw header row
    DrawRectangle(content_x, y, content_width, ROW_HEIGHT, g_theme.sidebar);

    int header_x = content_x + PADDING;
    DrawTextCustom("Name", header_x + ICON_WIDTH, y + (ROW_HEIGHT - FONT_SIZE_SMALL) / 2,
             FONT_SIZE_SMALL, g_theme.textSecondary);
    header_x += NAME_COL_WIDTH;

    DrawTextCustom("Size", header_x, y + (ROW_HEIGHT - FONT_SIZE_SMALL) / 2,
             FONT_SIZE_SMALL, g_theme.textSecondary);
    header_x += SIZE_COL_WIDTH;

    DrawTextCustom("Modified", header_x, y + (ROW_HEIGHT - FONT_SIZE_SMALL) / 2,
             FONT_SIZE_SMALL, g_theme.textSecondary);

    y += ROW_HEIGHT;

    // Draw separator
    DrawLine(content_x, y, content_x + content_width, y, g_theme.border);

    // Show error message if any
    if (dir->error_message[0] != '\0') {
        DrawTextCustom(dir->error_message, content_x + PADDING, y + PADDING, FONT_SIZE, g_theme.error);
        return;
    }

    // Show empty directory message
    if (dir->count == 0) {
        DrawTextCustom("Empty folder", content_x + PADDING, y + PADDING, FONT_SIZE, g_theme.textSecondary);
        return;
    }

    // Draw file entries with virtual scrolling
    int browser_height = app->height - STATUSBAR_HEIGHT - ROW_HEIGHT - content_offset;
    int visible_count = browser_height / ROW_HEIGHT;

    for (int i = 0; i < visible_count && (app->scroll_offset + i) < dir->count; i++) {
        int entry_index = app->scroll_offset + i;
        FileEntry *entry = &dir->entries[entry_index];

        int row_y = y + (i * ROW_HEIGHT);
        bool selected = is_entry_selected(app, entry_index);
        bool is_cursor = (entry_index == app->selected_index);

        // Check if item is in clipboard for visual feedback
        OperationType clipboard_op = get_clipboard_operation(app, entry->path);

        // Draw selection background
        if (selected) {
            Color bg_color = is_cursor ? g_theme.selection : Fade(g_theme.selection, 0.6f);
            DrawRectangle(content_x, row_y, content_width, ROW_HEIGHT, bg_color);
        }

        int x = content_x + PADDING;

        // Icon - apply clipboard feedback
        Color icon_color = entry->is_directory ? g_theme.folder : g_theme.file;
        icon_color = apply_clipboard_feedback(icon_color, clipboard_op);
        const char *icon = get_file_icon(entry);
        DrawTextCustom(icon, x, row_y + (ROW_HEIGHT - FONT_SIZE) / 2, FONT_SIZE, icon_color);
        x += ICON_WIDTH;

        // Check if this entry is being renamed
        bool is_renaming = (app->rename_mode && app->rename_index == entry_index);

        if (is_renaming) {
            // Draw rename text field
            int field_width = NAME_COL_WIDTH - ICON_WIDTH - PADDING * 2;
            int field_height = ROW_HEIGHT - 4;
            int field_y = row_y + 2;

            // Text field background
            DrawRectangle(x, field_y, field_width, field_height, g_theme.background);
            DrawRectangleLinesEx((Rectangle){(float)x, (float)field_y, (float)field_width, (float)field_height},
                                 1, g_theme.accent);

            // Draw rename text
            DrawTextCustom(app->rename_buffer, x + 2, field_y + (field_height - FONT_SIZE) / 2,
                     FONT_SIZE, g_theme.textPrimary);

            // Draw cursor (blinking)
            float blink = GetTime();
            if ((int)(blink * 2) % 2 == 0) {
                char temp[NAME_MAX_LEN];
                strncpy(temp, app->rename_buffer, app->rename_cursor);
                temp[app->rename_cursor] = '\0';
                int cursor_x = x + 2 + MeasureTextCustom(temp, FONT_SIZE);
                DrawRectangle(cursor_x, field_y + 2, 2, field_height - 4, g_theme.accent);
            }
        } else {
            // Name - color based on git status if in repo
            Color name_color = entry->is_hidden ? g_theme.textSecondary : g_theme.textPrimary;
            if (entry->is_symlink) {
                name_color = g_theme.accent;
            } else if (app->git.is_repo && entry->git_status != FILE_GIT_NONE) {
                name_color = get_git_status_color(entry->git_status);
            }
            // Apply clipboard feedback to name color
            name_color = apply_clipboard_feedback(name_color, clipboard_op);

            // Truncate name if too long
            char display_name[128];
            int max_name_chars = (NAME_COL_WIDTH - ICON_WIDTH - PADDING * 2) / 8;
            if ((int)strlen(entry->name) > max_name_chars) {
                strncpy(display_name, entry->name, max_name_chars - 3);
                display_name[max_name_chars - 3] = '\0';
                strcat(display_name, "...");
            } else {
                strncpy(display_name, entry->name, sizeof(display_name) - 1);
                display_name[sizeof(display_name) - 1] = '\0';
            }

            DrawTextCustom(display_name, x, row_y + (ROW_HEIGHT - FONT_SIZE) / 2, FONT_SIZE, name_color);

            // Draw git status indicator after filename
            if (app->git.is_repo && entry->git_status != FILE_GIT_NONE) {
                char git_char[2] = { get_git_status_char(entry->git_status), '\0' };
                int name_width = MeasureTextCustom(display_name, FONT_SIZE);
                Color git_color = apply_clipboard_feedback(get_git_status_color(entry->git_status), clipboard_op);
                DrawTextCustom(git_char, x + name_width + 4, row_y + (ROW_HEIGHT - FONT_SIZE) / 2,
                         FONT_SIZE, git_color);
            }
        }

        x = content_x + PADDING + NAME_COL_WIDTH;

        // Size - apply clipboard feedback
        Color size_color = apply_clipboard_feedback(g_theme.textSecondary, clipboard_op);
        if (!entry->is_directory) {
            char size_str[32];
            format_file_size(entry->size, size_str, sizeof(size_str));
            DrawTextCustom(size_str, x, row_y + (ROW_HEIGHT - FONT_SIZE) / 2, FONT_SIZE, size_color);
        } else {
            DrawTextCustom("--", x, row_y + (ROW_HEIGHT - FONT_SIZE) / 2, FONT_SIZE, size_color);
        }
        x += SIZE_COL_WIDTH;

        // Modified date - apply clipboard feedback
        char date_str[32];
        format_modified_time(entry->modified, date_str, sizeof(date_str));
        Color date_color = apply_clipboard_feedback(g_theme.textSecondary, clipboard_op);
        DrawTextCustom(date_str, x, row_y + (ROW_HEIGHT - FONT_SIZE) / 2, FONT_SIZE, date_color);
    }

    // Draw scrollbar if needed
    if (dir->count > visible_count) {
        int scrollbar_height = browser_height;
        int thumb_height = (visible_count * scrollbar_height) / dir->count;
        if (thumb_height < 20) thumb_height = 20;

        int thumb_y = y + (app->scroll_offset * (scrollbar_height - thumb_height)) / (dir->count - visible_count);

        // Scrollbar track
        DrawRectangle(content_x + content_width - SCROLLBAR_WIDTH, y, SCROLLBAR_WIDTH, scrollbar_height, g_theme.sidebar);

        // Scrollbar thumb
        DrawRectangle(content_x + content_width - SCROLLBAR_WIDTH + 2, thumb_y, SCROLLBAR_WIDTH - 4, thumb_height, g_theme.selection);
    }
}

// Draw grid view
static void browser_draw_grid(App *app)
{
    DirectoryState *dir = &app->directory;
    int content_x = sidebar_get_content_x(app);
    int content_width = get_browser_width(app);
    int content_offset = get_content_offset_y(app);
    int content_height = app->height - STATUSBAR_HEIGHT - content_offset;

    // Show error message if any
    if (dir->error_message[0] != '\0') {
        DrawTextCustom(dir->error_message, content_x + PADDING, content_offset + PADDING, FONT_SIZE, g_theme.error);
        return;
    }

    // Show empty directory message
    if (dir->count == 0) {
        DrawTextCustom("Empty folder", content_x + PADDING, content_offset + PADDING, FONT_SIZE, g_theme.textSecondary);
        return;
    }

    int cols = (content_width - PADDING * 2) / GRID_ITEM_WIDTH;
    if (cols < 1) cols = 1;

    int visible_rows = (content_height - PADDING * 2) / GRID_ITEM_HEIGHT;
    int total_rows = (dir->count + cols - 1) / cols;

    // Calculate scroll row
    int scroll_row = app->scroll_offset / cols;
    int start_row = scroll_row;
    int end_row = start_row + visible_rows + 1;
    if (end_row > total_rows) end_row = total_rows;

    for (int row = start_row; row < end_row; row++) {
        for (int col = 0; col < cols; col++) {
            int index = row * cols + col;
            if (index >= dir->count) break;

            FileEntry *entry = &dir->entries[index];

            int x = content_x + PADDING + col * GRID_ITEM_WIDTH;
            int y = content_offset + PADDING + (row - scroll_row) * GRID_ITEM_HEIGHT;

            bool selected = is_entry_selected(app, index);
            bool is_cursor = (index == app->selected_index);

            // Check if item is in clipboard for visual feedback
            OperationType clipboard_op = get_clipboard_operation(app, entry->path);

            // Draw selection background
            if (selected) {
                Color bg_color = is_cursor ? g_theme.selection : Fade(g_theme.selection, 0.6f);
                DrawRectangle(x, y, GRID_ITEM_WIDTH - 4, GRID_ITEM_HEIGHT - 4, bg_color);
            }

            // Draw icon (large, centered) - apply clipboard feedback
            Color icon_color = entry->is_directory ? g_theme.folder : g_theme.file;
            icon_color = apply_clipboard_feedback(icon_color, clipboard_op);
            const char *icon = get_file_icon(entry);

            int icon_x = x + (GRID_ITEM_WIDTH - 4) / 2 - 12;
            int icon_y = y + 10;
            DrawTextCustom(icon, icon_x, icon_y, 20, icon_color);

            // Draw name (truncated, centered)
            char display_name[32];
            int max_chars = (GRID_ITEM_WIDTH - 8) / 7;
            if ((int)strlen(entry->name) > max_chars) {
                strncpy(display_name, entry->name, max_chars - 2);
                display_name[max_chars - 2] = '\0';
                strcat(display_name, "..");
            } else {
                strncpy(display_name, entry->name, sizeof(display_name) - 1);
                display_name[sizeof(display_name) - 1] = '\0';
            }

            int text_width = MeasureTextCustom(display_name, FONT_SIZE_SMALL);
            int text_x = x + (GRID_ITEM_WIDTH - 4 - text_width) / 2;
            int text_y = y + GRID_ITEM_HEIGHT - GRID_TEXT_HEIGHT;

            // Color based on git status if in repo - apply clipboard feedback
            Color name_color = entry->is_hidden ? g_theme.textSecondary : g_theme.textPrimary;
            if (app->git.is_repo && entry->git_status != FILE_GIT_NONE) {
                name_color = get_git_status_color(entry->git_status);
            }
            name_color = apply_clipboard_feedback(name_color, clipboard_op);
            DrawTextCustom(display_name, text_x, text_y, FONT_SIZE_SMALL, name_color);

            // Draw git status indicator - apply clipboard feedback
            if (app->git.is_repo && entry->git_status != FILE_GIT_NONE) {
                char git_char[2] = { get_git_status_char(entry->git_status), '\0' };
                int indicator_x = x + GRID_ITEM_WIDTH - 14;
                int indicator_y = y + 2;
                Color git_color = apply_clipboard_feedback(get_git_status_color(entry->git_status), clipboard_op);
                DrawTextCustom(git_char, indicator_x, indicator_y, FONT_SIZE_SMALL, git_color);
            }
        }
    }
}

// Draw a single column for column view
static void draw_column(App *app, DirectoryState *dir, int col_x, int col_width, int selected_index, int scroll_offset)
{
    int content_offset = get_content_offset_y(app);
    int content_height = app->height - STATUSBAR_HEIGHT - content_offset;
    int visible_count = content_height / ROW_HEIGHT;

    // Column background
    DrawRectangle(col_x, content_offset, col_width, content_height, g_theme.background);

    // Right border
    DrawLine(col_x + col_width - 1, content_offset, col_x + col_width - 1, content_offset + content_height, g_theme.border);

    if (dir->error_message[0] != '\0') {
        DrawTextCustom("Error", col_x + PADDING, content_offset + PADDING, FONT_SIZE_SMALL, g_theme.error);
        return;
    }

    if (dir->count == 0) {
        DrawTextCustom("Empty", col_x + PADDING, content_offset + PADDING, FONT_SIZE_SMALL, g_theme.textSecondary);
        return;
    }

    for (int i = 0; i < visible_count && (scroll_offset + i) < dir->count; i++) {
        int entry_index = scroll_offset + i;
        FileEntry *entry = &dir->entries[entry_index];

        int row_y = content_offset + i * ROW_HEIGHT;
        bool is_selected = (entry_index == selected_index);

        if (is_selected) {
            DrawRectangle(col_x, row_y, col_width - 1, ROW_HEIGHT, g_theme.selection);
        }

        // Icon
        Color icon_color = entry->is_directory ? g_theme.folder : g_theme.file;
        const char *icon = entry->is_directory ? ">" : " ";
        DrawTextCustom(icon, col_x + PADDING, row_y + (ROW_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, icon_color);

        // Name
        char display_name[64];
        int max_chars = (col_width - PADDING * 3 - 10) / 7;
        if ((int)strlen(entry->name) > max_chars && max_chars > 3) {
            strncpy(display_name, entry->name, max_chars - 2);
            display_name[max_chars - 2] = '\0';
            strcat(display_name, "..");
        } else {
            strncpy(display_name, entry->name, sizeof(display_name) - 1);
            display_name[sizeof(display_name) - 1] = '\0';
        }

        Color name_color = entry->is_hidden ? g_theme.textSecondary : g_theme.textPrimary;
        DrawTextCustom(display_name, col_x + PADDING + 15, row_y + (ROW_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, name_color);

        // Directory indicator
        if (entry->is_directory) {
            DrawTextCustom(">", col_x + col_width - 15, row_y + (ROW_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, g_theme.textSecondary);
        }
    }
}

// Draw file preview pane in rightmost column (for column view)
static void draw_preview_column(App *app, FileEntry *entry, int col_x, int col_width)
{
    int content_offset = get_content_offset_y(app);
    int preview_height = app->height - STATUSBAR_HEIGHT - content_offset;

    // Column background
    DrawRectangle(col_x, content_offset, col_width, preview_height, g_theme.background);
    DrawLine(col_x + col_width - 1, content_offset, col_x + col_width - 1, content_offset + preview_height, g_theme.border);

    int y = content_offset + PADDING;
    int x = col_x + PADDING;
    int text_width = col_width - PADDING * 2;

    // File name (header)
    DrawTextCustom("Preview", x, y, FONT_SIZE, g_theme.textPrimary);
    y += ROW_HEIGHT + PADDING;

    // Truncated filename
    char display_name[128];
    int max_chars = text_width / 7;
    if ((int)strlen(entry->name) > max_chars && max_chars > 3) {
        strncpy(display_name, entry->name, max_chars - 2);
        display_name[max_chars - 2] = '\0';
        strcat(display_name, "..");
    } else {
        strncpy(display_name, entry->name, sizeof(display_name) - 1);
        display_name[sizeof(display_name) - 1] = '\0';
    }
    DrawTextCustom(display_name, x, y, FONT_SIZE_SMALL, g_theme.accent);
    y += ROW_HEIGHT;

    // Size
    char size_str[32];
    format_file_size(entry->size, size_str, sizeof(size_str));
    char info[128];
    snprintf(info, sizeof(info), "Size: %s", size_str);
    DrawTextCustom(info, x, y, FONT_SIZE_SMALL, g_theme.textSecondary);
    y += ROW_HEIGHT;

    // Type
    const char *ext = entry->extension;
    if (ext && ext[0] != '\0') {
        snprintf(info, sizeof(info), "Type: .%s file", ext);
    } else {
        snprintf(info, sizeof(info), "Type: Document");
    }
    DrawTextCustom(info, x, y, FONT_SIZE_SMALL, g_theme.textSecondary);
    y += ROW_HEIGHT;

    // Modified date
    char date_str[32];
    format_modified_time(entry->modified, date_str, sizeof(date_str));
    snprintf(info, sizeof(info), "Modified: %s", date_str);
    DrawTextCustom(info, x, y, FONT_SIZE_SMALL, g_theme.textSecondary);
    y += ROW_HEIGHT * 2;

    // For text files, show preview content
    if (strcmp(ext, "txt") == 0 || strcmp(ext, "md") == 0 ||
        strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0 ||
        strcmp(ext, "py") == 0 || strcmp(ext, "js") == 0 ||
        strcmp(ext, "json") == 0 || strcmp(ext, "yml") == 0) {

        DrawLine(x, y, col_x + col_width - PADDING, y, g_theme.border);
        y += PADDING;

        FILE *f = fopen(entry->path, "r");
        if (f) {
            char line[256];
            int lines_shown = 0;
            int max_lines = (preview_height - (y - content_offset)) / (FONT_SIZE_SMALL + 2);

            while (fgets(line, sizeof(line), f) && lines_shown < max_lines) {
                // Remove newline
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

                // Truncate long lines
                int max_line_chars = text_width / 7;
                if ((int)strlen(line) > max_line_chars && max_line_chars > 3) {
                    line[max_line_chars - 3] = '\0';
                    strcat(line, "...");
                }

                DrawTextCustom(line, x, y, FONT_SIZE_SMALL, g_theme.textSecondary);
                y += FONT_SIZE_SMALL + 2;
                lines_shown++;
            }
            fclose(f);
        }
    }
}

// Initialize column state for current path
static void column_state_init_from_path(ColumnState *cols, const char *current_path)
{
    memset(cols, 0, sizeof(ColumnState));
    cols->h_scroll = 0;
    cols->visible_columns = 3;

    // Parse current path into column paths (parent hierarchy)
    // e.g., /Users/foo/bar -> columns: ["/", "/Users", "/Users/foo", "/Users/foo/bar"]
    char path_copy[PATH_MAX_LEN];
    strncpy(path_copy, current_path, PATH_MAX_LEN - 1);
    path_copy[PATH_MAX_LEN - 1] = '\0';

    // Always start with root
    strncpy(cols->paths[0], "/", PATH_MAX_LEN);
    cols->column_count = 1;

    if (strcmp(current_path, "/") != 0) {
        char *token = strtok(path_copy, "/");
        char build_path[PATH_MAX_LEN] = "";

        while (token != NULL && cols->column_count < MAX_COLUMN_DEPTH) {
            strcat(build_path, "/");
            strcat(build_path, token);
            strncpy(cols->paths[cols->column_count], build_path, PATH_MAX_LEN - 1);
            cols->column_count++;
            token = strtok(NULL, "/");
        }
    }

    // Initialize all selected indices to 0
    for (int i = 0; i < cols->column_count; i++) {
        cols->selected[i] = 0;
        cols->scroll[i] = 0;
    }
}

// Update column state - adjust horizontal scroll to show current column
static void column_state_ensure_visible(ColumnState *cols, int current_column)
{
    // Ensure the current column is visible
    if (current_column < cols->h_scroll) {
        cols->h_scroll = current_column;
    }

    // Reserve one column for preview, so visible area is visible_columns - 1
    int last_visible = cols->h_scroll + cols->visible_columns - 2;
    if (current_column > last_visible) {
        cols->h_scroll = current_column - (cols->visible_columns - 2);
    }

    if (cols->h_scroll < 0) cols->h_scroll = 0;
}

// Draw column view (Miller columns) with horizontal scrolling
static void browser_draw_column(App *app)
{
    int content_x = sidebar_get_content_x(app);
    int content_width = get_browser_width(app);
    int content_offset = get_content_offset_y(app);
    int content_height = app->height - STATUSBAR_HEIGHT - content_offset;

    DirectoryState *dir = &app->directory;
    ColumnState *cols = &app->columns;

    // Calculate how many columns we can fit (including preview column)
    cols->visible_columns = content_width / COLUMN_VIEW_WIDTH;
    if (cols->visible_columns < 2) cols->visible_columns = 2;
    if (cols->visible_columns > 5) cols->visible_columns = 5;

    int col_width = content_width / cols->visible_columns;

    // Initialize column state if needed (first time or path changed)
    if (strcmp(cols->last_initialized_path, dir->current_path) != 0) {
        column_state_init_from_path(cols, dir->current_path);
        strncpy(cols->last_initialized_path, dir->current_path, PATH_MAX_LEN - 1);
    }

    // The current directory is the last column in our chain
    int current_col = cols->column_count - 1;

    // Ensure the current column is visible with horizontal scrolling
    column_state_ensure_visible(cols, current_col);

    // Draw horizontal scroll indicator if needed
    if (cols->h_scroll > 0) {
        // Left arrow indicator
        DrawTextCustom("<", content_x + 5, content_offset + content_height / 2, FONT_SIZE, g_theme.accent);
    }

    // Draw visible columns
    int draw_x = content_x;
    for (int i = 0; i < cols->visible_columns - 1 && (cols->h_scroll + i) < cols->column_count; i++) {
        int col_index = cols->h_scroll + i;
        bool is_current = (col_index == current_col);

        // Load directory for this column
        DirectoryState col_dir;
        directory_state_init(&col_dir);
        bool loaded = directory_read(&col_dir, cols->paths[col_index]);

        if (loaded) {
            // Determine selection for this column
            int sel_index = -1;
            int scroll_off = 0;

            if (is_current) {
                // Current column uses app's selection
                sel_index = app->selected_index;
                scroll_off = app->scroll_offset;
            } else if (col_index < cols->column_count - 1) {
                // For parent columns, find which entry leads to the next path
                const char *next_path = cols->paths[col_index + 1];
                for (int j = 0; j < col_dir.count; j++) {
                    if (strcmp(col_dir.entries[j].path, next_path) == 0) {
                        sel_index = j;
                        break;
                    }
                }
            }

            // Draw the column
            draw_column(app, &col_dir, draw_x, col_width, sel_index, scroll_off);
        } else {
            // Draw empty column with error
            DrawRectangle(draw_x, content_offset, col_width, content_height, g_theme.background);
            DrawLine(draw_x + col_width - 1, content_offset, draw_x + col_width - 1, content_offset + content_height, g_theme.border);
            DrawTextCustom("Error", draw_x + PADDING, content_offset + PADDING, FONT_SIZE_SMALL, g_theme.error);
        }

        directory_state_free(&col_dir);
        draw_x += col_width;
    }

    // Draw preview column (rightmost)
    if (dir->count > 0) {
        FileEntry *selected = &dir->entries[app->selected_index];
        if (selected->is_directory) {
            // Preview shows directory contents
            DirectoryState preview_dir;
            directory_state_init(&preview_dir);
            if (directory_read(&preview_dir, selected->path)) {
                draw_column(app, &preview_dir, draw_x, col_width, -1, 0);
            } else {
                // Empty preview column
                DrawRectangle(draw_x, content_offset, col_width, content_height, g_theme.background);
                DrawLine(draw_x + col_width - 1, content_offset, draw_x + col_width - 1, content_offset + content_height, g_theme.border);
                DrawTextCustom("Empty", draw_x + PADDING, content_offset + PADDING, FONT_SIZE_SMALL, g_theme.textSecondary);
            }
            directory_state_free(&preview_dir);
        } else {
            // Preview shows file info
            draw_preview_column(app, selected, draw_x, col_width);
        }
    } else {
        // Empty preview column
        DrawRectangle(draw_x, content_offset, col_width, content_height, g_theme.background);
        DrawLine(draw_x + col_width - 1, content_offset, draw_x + col_width - 1, content_offset + content_height, g_theme.border);
    }

    // Draw right scroll indicator if more columns exist
    if (cols->h_scroll + cols->visible_columns - 1 < cols->column_count) {
        DrawTextCustom(">", content_x + content_width - 15, content_offset + content_height / 2, FONT_SIZE, g_theme.accent);
    }
}

void browser_draw(App *app)
{
    switch (app->view_mode) {
        case VIEW_LIST:
            browser_draw_list(app);
            break;
        case VIEW_GRID:
            browser_draw_grid(app);
            break;
        case VIEW_COLUMN:
            browser_draw_column(app);
            break;
    }
}

void browser_ensure_visible(App *app)
{
    int content_offset = get_content_offset_y(app);
    int content_height = app->height - STATUSBAR_HEIGHT - content_offset;

    if (app->view_mode == VIEW_GRID) {
        // Grid view scrolling
        int cols = app->grid_cols;
        if (cols < 1) cols = 1;

        int visible_rows = content_height / GRID_ITEM_HEIGHT;
        int selected_row = app->selected_index / cols;
        int scroll_row = app->scroll_offset / cols;

        if (selected_row < scroll_row) {
            app->scroll_offset = selected_row * cols;
        }
        if (selected_row >= scroll_row + visible_rows) {
            app->scroll_offset = (selected_row - visible_rows + 1) * cols;
        }
    } else {
        // List/Column view scrolling
        int browser_height = content_height - ROW_HEIGHT;
        int visible_count = browser_height / ROW_HEIGHT;

        if (visible_count <= 0) {
            visible_count = 1;
        }

        if (app->selected_index < app->scroll_offset) {
            app->scroll_offset = app->selected_index;
        }

        if (app->selected_index >= app->scroll_offset + visible_count) {
            app->scroll_offset = app->selected_index - visible_count + 1;
        }

        int max_scroll = app->directory.count - visible_count;
        if (max_scroll < 0) max_scroll = 0;

        if (app->scroll_offset > max_scroll) {
            app->scroll_offset = max_scroll;
        }
        if (app->scroll_offset < 0) {
            app->scroll_offset = 0;
        }
    }
}

// ============================================================================
// HIT TESTING - Convert mouse position to file index
// ============================================================================

// Content area helper - shared by all hit-test functions
typedef struct ContentArea {
    int x;
    int y;
    int width;
    int height;
} ContentArea;

static ContentArea get_content_area(App *app)
{
    int x = sidebar_get_content_x(app);
    int y = get_content_offset_y(app);
    int w = get_browser_width(app);
    int h = app->height - STATUSBAR_HEIGHT - y;
    return (ContentArea){x, y, w, h};
}

static bool point_in_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

// Hit test for list view - returns file index or -1
static int browser_list_hit_test(App *app, Vector2 mouse_pos)
{
    ContentArea area = get_content_area(app);
    int list_start_y = area.y + ROW_HEIGHT;  // After header row
    int list_height = area.height - ROW_HEIGHT;

    if (!point_in_rect((int)mouse_pos.x, (int)mouse_pos.y,
                       area.x, list_start_y, area.width, list_height)) {
        return -1;
    }

    int row = (int)(mouse_pos.y - list_start_y) / ROW_HEIGHT;
    int index = app->scroll_offset + row;
    return (index >= 0 && index < app->directory.count) ? index : -1;
}

// Hit test for grid view - O(1) calculation instead of O(n) loop
static int browser_grid_hit_test(App *app, Vector2 mouse_pos)
{
    ContentArea area = get_content_area(app);

    // Calculate grid layout
    int cols = (area.width - PADDING * 2) / GRID_ITEM_WIDTH;
    if (cols < 1) cols = 1;

    // Relative position within grid area
    int rel_x = (int)mouse_pos.x - area.x - PADDING;
    int rel_y = (int)mouse_pos.y - area.y - PADDING;

    if (rel_x < 0 || rel_y < 0) return -1;
    if (rel_x >= cols * GRID_ITEM_WIDTH) return -1;

    int col = rel_x / GRID_ITEM_WIDTH;
    int scroll_row = app->scroll_offset / cols;
    int row = rel_y / GRID_ITEM_HEIGHT + scroll_row;

    // Check if within item bounds (not in padding between items)
    int item_x = rel_x % GRID_ITEM_WIDTH;
    int item_y = rel_y % GRID_ITEM_HEIGHT;
    if (item_x >= GRID_ITEM_WIDTH - 4 || item_y >= GRID_ITEM_HEIGHT - 4) {
        return -1;  // In the gap between items
    }

    if (col >= cols) return -1;

    int index = row * cols + col;
    return (index >= 0 && index < app->directory.count) ? index : -1;
}

// Hit test for column view - only tests current (rightmost data) column
static int browser_column_hit_test(App *app, Vector2 mouse_pos)
{
    ContentArea area = get_content_area(app);
    ColumnState *cols = &app->columns;

    if (cols->visible_columns < 1) return -1;
    int col_width = area.width / cols->visible_columns;

    // Find the current column's screen position
    int current_col = cols->column_count - 1;
    int col_screen_pos = current_col - cols->h_scroll;

    // Column is not visible or is the preview column (rightmost)
    if (col_screen_pos < 0 || col_screen_pos >= cols->visible_columns - 1) {
        return -1;
    }

    int col_x = area.x + col_screen_pos * col_width;

    // Check if mouse is in the current column
    if (!point_in_rect((int)mouse_pos.x, (int)mouse_pos.y,
                       col_x, area.y, col_width, area.height)) {
        return -1;
    }

    int row = (int)(mouse_pos.y - area.y) / ROW_HEIGHT;
    int index = app->scroll_offset + row;
    return (index >= 0 && index < app->directory.count) ? index : -1;
}

// Unified hit test - dispatches based on view mode
static int browser_hit_test(App *app, Vector2 mouse_pos)
{
    switch (app->view_mode) {
        case VIEW_LIST:   return browser_list_hit_test(app, mouse_pos);
        case VIEW_GRID:   return browser_grid_hit_test(app, mouse_pos);
        case VIEW_COLUMN: return browser_column_hit_test(app, mouse_pos);
        default:          return -1;
    }
    return -1;
}

// ============================================================================
// GRID VIEW HELPERS
// ============================================================================

// Get grid item bounds for a given index
static Rectangle get_grid_item_bounds(App *app, int index)
{
    int content_x = sidebar_get_content_x(app);
    int content_width = get_browser_width(app);
    int content_offset = get_content_offset_y(app);

    int cols = (content_width - PADDING * 2) / GRID_ITEM_WIDTH;
    if (cols < 1) cols = 1;

    int scroll_row = app->scroll_offset / cols;
    int row = index / cols;
    int col = index % cols;

    int x = content_x + PADDING + col * GRID_ITEM_WIDTH;
    int y = content_offset + PADDING + (row - scroll_row) * GRID_ITEM_HEIGHT;

    return (Rectangle){(float)x, (float)y, (float)(GRID_ITEM_WIDTH - 4), (float)(GRID_ITEM_HEIGHT - 4)};
}

// Check if rectangle intersects with another rectangle
static bool rects_intersect(Rectangle a, Rectangle b)
{
    return !(a.x + a.width < b.x || b.x + b.width < a.x ||
             a.y + a.height < b.y || b.y + b.height < a.y);
}

// Get normalized rectangle (positive width/height) from two points
static Rectangle get_selection_rect(Vector2 start, Vector2 end)
{
    float x = (start.x < end.x) ? start.x : end.x;
    float y = (start.y < end.y) ? start.y : end.y;
    float w = fabsf(end.x - start.x);
    float h = fabsf(end.y - start.y);
    return (Rectangle){x, y, w, h};
}

// ============================================================================
// HELPER: Safe string copy with null termination
// ============================================================================
static void safe_strcpy(char *dest, const char *src, size_t dest_size)
{
    if (dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// ============================================================================
// HELPER: Check if file extension is summarizable
// ============================================================================
static bool is_summarizable_extension(const char *ext)
{
    if (!ext || ext[0] == '\0') return false;
    return strcmp(ext, "txt") == 0 ||
           strcmp(ext, "md") == 0 ||
           strcmp(ext, "log") == 0 ||
           strcmp(ext, "json") == 0 ||
           strcmp(ext, "xml") == 0 ||
           strcmp(ext, "csv") == 0;
}

// ============================================================================
// HOVER: Update hover state and cancel pending requests if hover changed
// ============================================================================
static void browser_update_hover(App *app, Vector2 mouse_pos, double current_time)
{
    BrowserState *bs = &app->browser_state;
    int hovered_index = browser_hit_test(app, mouse_pos);

    if (hovered_index != bs->hovered_index) {
        // Cancel any pending async request
        if (bs->summary_state == HOVER_LOADING && atomic_load(&app->summary_thread_active)) {
            summarize_async_cancel(&app->async_summary_request);
            pthread_join(app->summary_thread, NULL);
            atomic_store(&app->summary_thread_active, false);
        }

        bs->hovered_index = hovered_index;
        bs->hover_start_time = current_time;
        bs->summary_state = (hovered_index < 0) ? HOVER_IDLE : HOVER_DEBOUNCING;
    }
}

// ============================================================================
// HOVER: Process debounce timer and trigger AI summary if ready
// ============================================================================
static void browser_update_summary_debounce(App *app, double current_time)
{
    BrowserState *bs = &app->browser_state;

    if (bs->summary_state != HOVER_DEBOUNCING || bs->hovered_index < 0) {
        return;
    }

    // Bounds check
    if (bs->hovered_index >= app->directory.count) {
        bs->summary_state = HOVER_IDLE;
        return;
    }

    double elapsed = current_time - bs->hover_start_time;
    if (elapsed < HOVER_DEBOUNCE_TIME) {
        return;
    }

    FileEntry *entry = &app->directory.entries[bs->hovered_index];

    if (entry->is_directory || !is_summarizable_extension(entry->extension)) {
        bs->summary_state = HOVER_IDLE;
        return;
    }

    // Check cache first (fast, synchronous)
    SummaryResult cached;
    if (app->summary_cache && summary_cache_get(app->summary_cache, entry->path, &cached)) {
        safe_strcpy(bs->summary_text, cached.summary, sizeof(bs->summary_text));
        safe_strcpy(bs->summary_path, entry->path, sizeof(bs->summary_path));
        bs->summary_state = HOVER_READY;

        if (!preview_is_visible(&app->preview)) {
            preview_toggle(&app->preview);
        }
        return;
    }

    // Cache miss - start async API call
    app->summary_config.default_level = SUMM_LEVEL_BRIEF;
    if (summarize_async_start(&app->async_summary_request, &app->summary_thread,
                              entry->path, &app->summary_config, app->summary_cache)) {
        atomic_store(&app->summary_thread_active, true);
        bs->summary_state = HOVER_LOADING;

        if (!preview_is_visible(&app->preview)) {
            preview_toggle(&app->preview);
        }
    } else {
        bs->summary_state = HOVER_ERROR;
        safe_strcpy(bs->summary_error, "Failed to start summary", sizeof(bs->summary_error));
    }
}

// ============================================================================
// HOVER: Poll async summary completion
// ============================================================================
static void browser_poll_async_summary(App *app)
{
    BrowserState *bs = &app->browser_state;

    if (bs->summary_state != HOVER_LOADING || !atomic_load(&app->summary_thread_active)) {
        return;
    }

    if (!summarize_async_is_complete(&app->async_summary_request)) {
        return;
    }

    pthread_join(app->summary_thread, NULL);
    atomic_store(&app->summary_thread_active, false);

    AsyncSummaryRequest *req = &app->async_summary_request;
    if (req->result.status == SUMM_STATUS_OK) {
        safe_strcpy(bs->summary_text, req->result.summary, sizeof(bs->summary_text));
        safe_strcpy(bs->summary_path, req->result.path, sizeof(bs->summary_path));
        bs->summary_state = HOVER_READY;
    } else {
        safe_strcpy(bs->summary_error, req->result.error_message, sizeof(bs->summary_error));
        bs->summary_state = HOVER_ERROR;
    }
}

// ============================================================================
// CLICK: Handle mouse click for selection and navigation
// ============================================================================
static void browser_handle_click(App *app, Vector2 mouse_pos, double current_time)
{
    BrowserState *bs = &app->browser_state;
    int clicked_index = browser_hit_test(app, mouse_pos);

    bool cmd_down = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    bool shift_down = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if (clicked_index >= 0) {
        // Check for double-click
        bool is_double_click = (clicked_index == bs->last_click_index) &&
                               (current_time - bs->last_click_time) < DOUBLE_CLICK_TIME;

        if (is_double_click && !cmd_down && !shift_down) {
            // Double-click: navigate into directory or open file view modal
            FileEntry *entry = &app->directory.entries[clicked_index];
            if (entry->is_directory) {
                if (directory_enter(&app->directory, clicked_index)) {
                    app->selected_index = 0;
                    app->scroll_offset = 0;
                    selection_clear(&app->selection);
                    history_push(&app->history, app->directory.current_path);
                    breadcrumb_update(&app->breadcrumb, app->directory.current_path);

                    if (app->view_mode == VIEW_COLUMN) {
                        app->columns.last_initialized_path[0] = '\0';
                    }
                }
            } else {
                // Double-click on file: open file view modal
                // Load the preview first to get content/texture
                preview_load(&app->preview, entry->path);

                PreviewType ptype = app->preview.type;
                if (ptype == PREVIEW_TEXT || ptype == PREVIEW_CODE || ptype == PREVIEW_MARKDOWN) {
                    // Open text in modal
                    if (app->preview.text_content) {
                        file_view_modal_show_text(&app->file_view_modal, entry->path,
                                                  app->preview.text_content);
                    }
                } else if (ptype == PREVIEW_IMAGE) {
                    // Open image in modal
                    if (app->preview.texture_id != 0) {
                        file_view_modal_show_image(&app->file_view_modal, entry->path,
                                                   app->preview.texture_id,
                                                   app->preview.image_width,
                                                   app->preview.image_height);
                    }
                }
            }
            bs->last_click_index = -1;
            bs->last_click_time = 0;
        } else {
            // Single click: selection handling
            if (cmd_down) {
                selection_toggle(&app->selection, clicked_index);
                app->selected_index = clicked_index;
            } else if (shift_down) {
                int anchor = app->selection.anchor_index >= 0 ?
                             app->selection.anchor_index : app->selected_index;
                selection_clear(&app->selection);
                selection_range(&app->selection, anchor, clicked_index);
                app->selected_index = clicked_index;
            } else {
                selection_clear(&app->selection);
                app->selected_index = clicked_index;
                app->selection.anchor_index = clicked_index;
            }

            bs->last_click_index = clicked_index;
            bs->last_click_time = current_time;

            // Auto-open preview on single click for files
            FileEntry *entry = &app->directory.entries[clicked_index];
            if (!entry->is_directory) {
                if (!preview_is_visible(&app->preview)) {
                    app->preview.visible = true;
                }
                preview_load(&app->preview, entry->path);
            }
        }

        browser_ensure_visible(app);
    } else {
        // Click on empty space
        if (app->view_mode == VIEW_GRID) {
            app->rubber_band_active = true;
            app->rubber_band_start = mouse_pos;
            app->rubber_band_current = mouse_pos;
        }

        if (!cmd_down && !shift_down) {
            selection_clear(&app->selection);
        }
        bs->last_click_index = -1;
    }
}

// ============================================================================
// RUBBER BAND: Update rubber band selection (grid view only)
// ============================================================================
static void browser_handle_rubber_band(App *app, Vector2 mouse_pos)
{
    if (!app->rubber_band_active || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            app->rubber_band_active = false;
        }
        return;
    }

    app->rubber_band_current = mouse_pos;

    Rectangle sel_rect = get_selection_rect(app->rubber_band_start, app->rubber_band_current);

    bool cmd_down = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (!cmd_down) {
        selection_clear(&app->selection);
    }

    for (int i = 0; i < app->directory.count; i++) {
        Rectangle item_bounds = get_grid_item_bounds(app, i);
        if (rects_intersect(sel_rect, item_bounds)) {
            selection_add(&app->selection, i);
        }
    }

    if (app->selection.count > 0) {
        app->selected_index = app->selection.indices[0];
    }
}

// ============================================================================
// MAIN: Handle browser mouse input
// ============================================================================
void browser_handle_input(App *app)
{
    Vector2 mouse_pos = GetMousePosition();
    double current_time = GetTime();

    // Update hover tracking
    browser_update_hover(app, mouse_pos, current_time);

    // Process async summary polling (summary now triggered by button, not auto-debounce)
    // browser_update_summary_debounce(app, current_time);  // Disabled: summary is now optional via button
    browser_poll_async_summary(app);

    // Handle click (check if in content area first)
    ContentArea area = get_content_area(app);
    bool in_content = point_in_rect((int)mouse_pos.x, (int)mouse_pos.y,
                                    area.x, area.y, area.width, area.height);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && in_content) {
        browser_handle_click(app, mouse_pos, current_time);
    }

    // Handle right-click for context menu
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_content) {
        int clicked_index = browser_hit_test(app, mouse_pos);

        // If right-clicking on unselected item, select it first
        if (clicked_index >= 0 && !selection_contains(&app->selection, clicked_index)) {
            selection_clear(&app->selection);
            app->selected_index = clicked_index;
        }

        context_menu_show(&app->context_menu, app,
                         (int)mouse_pos.x, (int)mouse_pos.y, clicked_index);
    }

    // Handle rubber band selection
    browser_handle_rubber_band(app, mouse_pos);
}

// Draw rubber band rectangle overlay (call after browser_draw)
void browser_draw_rubber_band(App *app)
{
    if (!app->rubber_band_active || app->view_mode != VIEW_GRID) {
        return;
    }

    Rectangle sel_rect = get_selection_rect(app->rubber_band_start, app->rubber_band_current);

    // Draw semi-transparent fill
    Color fill_color = Fade(g_theme.accent, 0.2f);
    DrawRectangleRec(sel_rect, fill_color);

    // Draw border
    Color border_color = g_theme.accent;
    DrawRectangleLinesEx(sel_rect, 1.0f, border_color);
}

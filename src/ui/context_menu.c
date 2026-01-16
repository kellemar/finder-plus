#include "context_menu.h"
#include "../app.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "../core/operations.h"
#include "../core/filesystem.h"
#include "raylib.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

// Menu dimensions
#define MENU_WIDTH 220
#define MENU_ITEM_HEIGHT 24
#define MENU_PADDING 4
#define MENU_SEPARATOR_HEIGHT 9

// Safe string copy helper
static inline void safe_strcpy(char *dest, const char *src, size_t dest_size)
{
    if (dest_size > 0) {
        strncpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
    }
}

// Case-insensitive extension comparison helper
static bool ext_matches(const char *ext, const char *target)
{
    if (!ext || !target) return false;
    while (*ext && *target) {
        if (tolower((unsigned char)*ext) != tolower((unsigned char)*target)) {
            return false;
        }
        ext++;
        target++;
    }
    return *ext == '\0' && *target == '\0';
}

// Check if file is an image (supports AI image editing)
static bool is_image_file(const char *extension)
{
    if (!extension || extension[0] == '\0') return false;
    // Skip leading dot if present
    if (extension[0] == '.') extension++;

    return ext_matches(extension, "png") ||
           ext_matches(extension, "jpg") ||
           ext_matches(extension, "jpeg") ||
           ext_matches(extension, "gif") ||
           ext_matches(extension, "bmp") ||
           ext_matches(extension, "webp");
}

// Check if file is a text file (supports AI summarization and editing)
static bool is_text_file(const char *extension)
{
    if (!extension || extension[0] == '\0') return false;
    // Skip leading dot if present
    if (extension[0] == '.') extension++;

    // Plain text
    if (ext_matches(extension, "txt") ||
        ext_matches(extension, "md") ||
        ext_matches(extension, "log")) return true;

    // Data formats
    if (ext_matches(extension, "json") ||
        ext_matches(extension, "xml") ||
        ext_matches(extension, "csv")) return true;

    // Code files
    if (ext_matches(extension, "c") ||
        ext_matches(extension, "h") ||
        ext_matches(extension, "py") ||
        ext_matches(extension, "js") ||
        ext_matches(extension, "ts") ||
        ext_matches(extension, "html") ||
        ext_matches(extension, "css")) return true;

    return false;
}

// Menu bounds for position adjustment
typedef struct {
    int x;
    int y;
    int height;
} MenuBounds;

// Calculate adjusted menu bounds to keep on screen
static MenuBounds calculate_menu_bounds(ContextMenuState *menu, int screen_width, int screen_height)
{
    MenuBounds bounds;
    bounds.height = MENU_PADDING * 2;
    for (int i = 0; i < menu->item_count; i++) {
        bounds.height += MENU_ITEM_HEIGHT;
        if (menu->items[i].separator_after) {
            bounds.height += MENU_SEPARATOR_HEIGHT;
        }
    }

    bounds.x = menu->x;
    bounds.y = menu->y;

    if (bounds.x + MENU_WIDTH > screen_width) {
        bounds.x = screen_width - MENU_WIDTH - 4;
    }
    if (bounds.y + bounds.height > screen_height - STATUSBAR_HEIGHT) {
        bounds.y = screen_height - STATUSBAR_HEIGHT - bounds.height - 4;
    }

    return bounds;
}

// Check if any menu items are enabled
static bool has_enabled_items(ContextMenuState *menu)
{
    for (int i = 0; i < menu->item_count; i++) {
        if (menu->items[i].enabled) {
            return true;
        }
    }
    return false;
}

// Navigate menu in given direction (+1 down, -1 up)
static void navigate_menu(ContextMenuState *menu, int direction)
{
    if (menu->item_count == 0 || !has_enabled_items(menu)) {
        return;
    }

    int start = menu->selected_index;
    do {
        menu->selected_index += direction;
        if (menu->selected_index >= menu->item_count) {
            menu->selected_index = 0;
        } else if (menu->selected_index < 0) {
            menu->selected_index = menu->item_count - 1;
        }
    } while (!menu->items[menu->selected_index].enabled &&
             menu->selected_index != start);
    menu->hovered_index = menu->selected_index;
}

#ifndef TESTING
// Forward declarations of action callbacks (non-test builds only)
static void action_open(struct App *app);
static void action_open_new_tab(struct App *app);
static void action_copy(struct App *app);
static void action_cut(struct App *app);
static void action_paste(struct App *app);
static void action_duplicate(struct App *app);
static void action_rename(struct App *app);
static void action_trash(struct App *app);
static void action_get_info(struct App *app);
static void action_new_folder(struct App *app);
// AI actions
static void action_edit_image_ai(struct App *app);
static void action_summarize(struct App *app);
static void action_edit_text_ai(struct App *app);
#else
// Stub action pointers for test builds
#define action_open NULL
#define action_open_new_tab NULL
#define action_copy NULL
#define action_cut NULL
#define action_paste NULL
#define action_duplicate NULL
#define action_rename NULL
#define action_trash NULL
#define action_get_info NULL
#define action_new_folder NULL
#define action_edit_image_ai NULL
#define action_summarize NULL
#define action_edit_text_ai NULL
#endif

// Helper to add a menu item
static void add_menu_item(ContextMenuState *menu, const char *label,
                          const char *shortcut, bool enabled,
                          bool separator_after, ContextMenuAction action)
{
    if (menu->item_count >= CONTEXT_MENU_MAX_ITEMS) {
        return;
    }

    ContextMenuItem *item = &menu->items[menu->item_count];
    safe_strcpy(item->label, label, sizeof(item->label));
    safe_strcpy(item->shortcut, shortcut, sizeof(item->shortcut));
    item->enabled = enabled;
    item->separator_after = separator_after;
    item->action = action;

    menu->item_count++;
}

void context_menu_init(ContextMenuState *menu)
{
    memset(menu, 0, sizeof(ContextMenuState));
    menu->target_index = -1;
    menu->hovered_index = -1;
}

void context_menu_show(ContextMenuState *menu, struct App *app,
                       int x, int y, int target_index)
{
    menu->visible = true;
    menu->x = x;
    menu->y = y;
    menu->target_index = target_index;
    menu->hovered_index = -1;
    menu->selected_index = 0;
    menu->item_count = 0;
    menu->target_path[0] = '\0';

    bool has_clipboard = clipboard_has_items(&app->clipboard);

    if (target_index < 0) {
        // Empty space context
        menu->type = CONTEXT_EMPTY_SPACE;

        add_menu_item(menu, "New Folder", "Cmd+Shift+N", true, false, action_new_folder);
        add_menu_item(menu, "Paste", "Cmd+V", has_clipboard, true, action_paste);
        add_menu_item(menu, "Get Info", "Cmd+I", true, false, action_get_info);

    } else if (app->selection.count > 1) {
        // Multi-select context
        menu->type = CONTEXT_MULTI_SELECT;
        safe_strcpy(menu->target_path, app->directory.entries[target_index].path,
                sizeof(menu->target_path));

        add_menu_item(menu, "Copy", "Cmd+C", true, false, action_copy);
        add_menu_item(menu, "Cut", "Cmd+X", true, false, action_cut);
        add_menu_item(menu, "Paste", "Cmd+V", has_clipboard, true, action_paste);
        add_menu_item(menu, "Duplicate", "Cmd+D", true, false, action_duplicate);
        add_menu_item(menu, "Move to Trash", "Cmd+Del", true, false, action_trash);

    } else {
        FileEntry *entry = &app->directory.entries[target_index];
        safe_strcpy(menu->target_path, entry->path, sizeof(menu->target_path));

        if (entry->is_directory) {
            // Folder context
            menu->type = CONTEXT_FOLDER;

            add_menu_item(menu, "Open", "", true, false, action_open);
            add_menu_item(menu, "Open in New Tab", "Cmd+T", true, true, action_open_new_tab);
            add_menu_item(menu, "Copy", "Cmd+C", true, false, action_copy);
            add_menu_item(menu, "Cut", "Cmd+X", true, false, action_cut);
            add_menu_item(menu, "Paste", "Cmd+V", has_clipboard, true, action_paste);
            add_menu_item(menu, "Duplicate", "Cmd+D", true, false, action_duplicate);
            add_menu_item(menu, "Rename", "F2", true, false, action_rename);
            add_menu_item(menu, "Move to Trash", "Cmd+Del", true, true, action_trash);
            add_menu_item(menu, "Get Info", "Cmd+I", true, false, action_get_info);

        } else {
            // File context
            menu->type = CONTEXT_FILE;

            add_menu_item(menu, "Open", "", true, true, action_open);
            add_menu_item(menu, "Copy", "Cmd+C", true, false, action_copy);
            add_menu_item(menu, "Cut", "Cmd+X", true, false, action_cut);
            add_menu_item(menu, "Paste", "Cmd+V", has_clipboard, true, action_paste);
            add_menu_item(menu, "Duplicate", "Cmd+D", true, false, action_duplicate);
            add_menu_item(menu, "Rename", "F2", true, false, action_rename);
            add_menu_item(menu, "Move to Trash", "Cmd+Del", true, true, action_trash);

            // AI actions based on file type
            bool is_image = is_image_file(entry->extension);
            bool is_text = is_text_file(entry->extension);

            if (is_image) {
                add_menu_item(menu, "Edit Image with AI", "", true, true, action_edit_image_ai);
            }
            if (is_text) {
                add_menu_item(menu, "Summarize", "", true, false, action_summarize);
                add_menu_item(menu, "Edit with AI", "", true, true, action_edit_text_ai);
            }

            add_menu_item(menu, "Get Info", "Cmd+I", true, false, action_get_info);
        }
    }

    // Find first enabled item for initial selection
    menu->selected_index = -1;
    for (int i = 0; i < menu->item_count; i++) {
        if (menu->items[i].enabled) {
            menu->selected_index = i;
            break;
        }
    }
}

void context_menu_hide(ContextMenuState *menu)
{
    menu->visible = false;
    menu->type = CONTEXT_NONE;
    menu->item_count = 0;
    menu->hovered_index = -1;
    menu->selected_index = -1;
    menu->target_index = -1;
    menu->target_path[0] = '\0';
}

bool context_menu_is_visible(ContextMenuState *menu)
{
    return menu->visible;
}

bool context_menu_handle_input(struct App *app)
{
    ContextMenuState *menu = &app->context_menu;

    if (!menu->visible) {
        return false;
    }

    MenuBounds bounds = calculate_menu_bounds(menu, app->width, app->height);
    Vector2 mouse = GetMousePosition();

    // Escape dismisses menu
    if (IsKeyPressed(KEY_ESCAPE)) {
        context_menu_hide(menu);
        return true;
    }

    // Keyboard navigation: j/k or down/up arrows
    if (IsKeyPressed(KEY_J) || IsKeyPressed(KEY_DOWN)) {
        navigate_menu(menu, 1);
        return true;
    }

    if (IsKeyPressed(KEY_K) || IsKeyPressed(KEY_UP)) {
        navigate_menu(menu, -1);
        return true;
    }

    // Enter executes selected item
    if (IsKeyPressed(KEY_ENTER)) {
        if (menu->selected_index >= 0 &&
            menu->selected_index < menu->item_count &&
            menu->items[menu->selected_index].enabled) {
            ContextMenuItem *item = &menu->items[menu->selected_index];
            // Execute action BEFORE hiding menu (actions need target_index)
            if (item->action) {
                item->action(app);
            }
            context_menu_hide(menu);
        }
        return true;
    }

    // Mouse hover detection
    int item_y = bounds.y + MENU_PADDING;
    menu->hovered_index = -1;

    for (int i = 0; i < menu->item_count; i++) {
        Rectangle item_rect = {
            (float)bounds.x, (float)item_y,
            (float)MENU_WIDTH, (float)MENU_ITEM_HEIGHT
        };

        if (CheckCollisionPointRec(mouse, item_rect)) {
            if (menu->items[i].enabled) {
                menu->hovered_index = i;
                menu->selected_index = i;
            }

            // Left click executes
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && menu->items[i].enabled) {
                ContextMenuItem *item = &menu->items[i];
                // Execute action BEFORE hiding menu (actions need target_index)
                if (item->action) {
                    item->action(app);
                }
                context_menu_hide(menu);
                return true;
            }
        }

        item_y += MENU_ITEM_HEIGHT;
        if (menu->items[i].separator_after) {
            item_y += MENU_SEPARATOR_HEIGHT;
        }
    }

    // Click outside menu dismisses it
    Rectangle menu_rect = {
        (float)bounds.x, (float)bounds.y,
        (float)MENU_WIDTH, (float)bounds.height
    };

    if ((IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) &&
        !CheckCollisionPointRec(mouse, menu_rect)) {
        context_menu_hide(menu);
        return true;
    }

    return true;  // Menu is visible, consume input
}

void context_menu_draw(struct App *app)
{
    ContextMenuState *menu = &app->context_menu;

    if (!menu->visible) {
        return;
    }

    MenuBounds bounds = calculate_menu_bounds(menu, app->width, app->height);

    // Draw shadow
    DrawRectangle(bounds.x + 3, bounds.y + 3, MENU_WIDTH, bounds.height, Fade(BLACK, 0.3f));

    // Draw menu background
    DrawRectangle(bounds.x, bounds.y, MENU_WIDTH, bounds.height, g_theme.sidebar);
    DrawRectangleLinesEx((Rectangle){(float)bounds.x, (float)bounds.y,
                         (float)MENU_WIDTH, (float)bounds.height},
                         1, g_theme.border);

    // Draw items
    int item_y = bounds.y + MENU_PADDING;
    for (int i = 0; i < menu->item_count; i++) {
        ContextMenuItem *item = &menu->items[i];

        // Hover/selection background
        if (i == menu->hovered_index && item->enabled) {
            DrawRectangle(bounds.x + 2, item_y, MENU_WIDTH - 4,
                         MENU_ITEM_HEIGHT, g_theme.selection);
        }

        // Item label
        Color text_color = item->enabled ? g_theme.textPrimary : g_theme.textSecondary;
        DrawTextCustom(item->label, bounds.x + MENU_PADDING + 4,
                 item_y + (MENU_ITEM_HEIGHT - FONT_SIZE_SMALL) / 2,
                 FONT_SIZE_SMALL, text_color);

        // Shortcut hint (right-aligned)
        if (item->shortcut[0] != '\0') {
            int shortcut_width = MeasureTextCustom(item->shortcut, FONT_SIZE_SMALL);
            DrawTextCustom(item->shortcut,
                     bounds.x + MENU_WIDTH - MENU_PADDING - 4 - shortcut_width,
                     item_y + (MENU_ITEM_HEIGHT - FONT_SIZE_SMALL) / 2,
                     FONT_SIZE_SMALL, g_theme.textSecondary);
        }

        item_y += MENU_ITEM_HEIGHT;

        // Separator
        if (item->separator_after) {
            int sep_y = item_y + MENU_SEPARATOR_HEIGHT / 2;
            DrawLine(bounds.x + MENU_PADDING, sep_y,
                    bounds.x + MENU_WIDTH - MENU_PADDING, sep_y, g_theme.border);
            item_y += MENU_SEPARATOR_HEIGHT;
        }
    }
}

#ifndef TESTING
// ============================================================
// Action Callbacks (only included in non-test builds)
// ============================================================

// Helper to collect paths from selection or context menu target
static int collect_selected_paths(struct App *app, const char *paths[], int max_paths)
{
    int path_count = 0;

    if (app->selection.count > 0) {
        for (int i = 0; i < app->selection.count && path_count < max_paths; i++) {
            int idx = app->selection.indices[i];
            if (idx >= 0 && idx < app->directory.count) {
                paths[path_count++] = app->directory.entries[idx].path;
            }
        }
    } else if (app->context_menu.target_index >= 0) {
        paths[path_count++] = app->context_menu.target_path;
    }

    return path_count;
}

static void action_open(struct App *app)
{
    ContextMenuState *menu = &app->context_menu;
    if (menu->target_index >= 0 && menu->target_index < app->directory.count) {
        FileEntry *entry = &app->directory.entries[menu->target_index];
        if (entry->is_directory) {
            // Navigate into directory
            directory_enter(&app->directory, menu->target_index);
            app->selected_index = 0;
            app->scroll_offset = 0;
            selection_clear(&app->selection);
            history_push(&app->history, app->directory.current_path);
            breadcrumb_update(&app->breadcrumb, app->directory.current_path);
        } else {
            // Open file with default application (macOS)
            char cmd[4200];
            snprintf(cmd, sizeof(cmd), "open \"%s\"", entry->path);
            system(cmd);
        }
    }
}

static void action_open_new_tab(struct App *app)
{
    ContextMenuState *menu = &app->context_menu;
    if (menu->target_index >= 0 && menu->target_index < app->directory.count) {
        FileEntry *entry = &app->directory.entries[menu->target_index];
        if (entry->is_directory) {
            tabs_new(&app->tabs, entry->path);
        }
    }
}

static void action_copy(struct App *app)
{
    const char *paths[MAX_SELECTION];
    int count = collect_selected_paths(app, paths, MAX_SELECTION);
    if (count > 0) {
        clipboard_copy(&app->clipboard, paths, count);
    }
}

static void action_cut(struct App *app)
{
    const char *paths[MAX_SELECTION];
    int count = collect_selected_paths(app, paths, MAX_SELECTION);
    if (count > 0) {
        clipboard_cut(&app->clipboard, paths, count);
    }
}

static void action_paste(struct App *app)
{
    clipboard_sync_from_system(&app->clipboard);
    if (clipboard_has_items(&app->clipboard)) {
        clipboard_paste(&app->clipboard, app->directory.current_path);
        directory_read(&app->directory, app->directory.current_path);
        selection_clear(&app->selection);
    }
}

static void action_duplicate(struct App *app)
{
    if (app->selection.count > 0) {
        for (int i = 0; i < app->selection.count; i++) {
            int idx = app->selection.indices[i];
            if (idx >= 0 && idx < app->directory.count) {
                file_duplicate(app->directory.entries[idx].path);
            }
        }
    } else if (app->context_menu.target_index >= 0) {
        file_duplicate(app->context_menu.target_path);
    }
    directory_read(&app->directory, app->directory.current_path);
    selection_clear(&app->selection);
}

static void action_rename(struct App *app)
{
    ContextMenuState *menu = &app->context_menu;
    if (menu->target_index >= 0 && menu->target_index < app->directory.count) {
        app->selected_index = menu->target_index;
        app->rename_mode = true;
        app->rename_index = menu->target_index;
        FileEntry *entry = &app->directory.entries[menu->target_index];
        safe_strcpy(app->rename_buffer, entry->name, NAME_MAX_LEN);
        app->rename_cursor = (int)strlen(app->rename_buffer);
    }
}

// Callback for trash confirmation dialog
static void perform_trash_confirmed(struct App *app)
{
    if (app->selection.count > 0) {
        for (int i = 0; i < app->selection.count; i++) {
            int idx = app->selection.indices[i];
            if (idx >= 0 && idx < app->directory.count) {
                file_delete(app->directory.entries[idx].path);
            }
        }
    } else if (app->context_menu.target_index >= 0 &&
               app->context_menu.target_path[0] != '\0') {
        file_delete(app->context_menu.target_path);
    }
    directory_read(&app->directory, app->directory.current_path);
    selection_clear(&app->selection);
    app->selected_index = 0;
}

static void action_trash(struct App *app)
{
    char message[512];
    int count = (app->selection.count > 0) ? app->selection.count : 1;

    if (count == 1) {
        const char *name = "";
        if (app->context_menu.target_index >= 0 &&
            app->context_menu.target_index < app->directory.count) {
            name = app->directory.entries[app->context_menu.target_index].name;
        }
        snprintf(message, sizeof(message), "Move \"%s\" to Trash?", name);
    } else {
        snprintf(message, sizeof(message), "Move %d items to Trash?", count);
    }

    dialog_confirm(&app->dialog, "Delete", message, perform_trash_confirmed);
}

static void action_get_info(struct App *app)
{
    ContextMenuState *menu = &app->context_menu;
    char message[512];

    if (menu->target_index >= 0 && menu->target_index < app->directory.count) {
        FileEntry *entry = &app->directory.entries[menu->target_index];

        // Format size
        char size_str[32];
        if (entry->size < 1024) {
            snprintf(size_str, sizeof(size_str), "%lld bytes", (long long)entry->size);
        } else if (entry->size < 1024 * 1024) {
            snprintf(size_str, sizeof(size_str), "%.1f KB", entry->size / 1024.0);
        } else if (entry->size < 1024 * 1024 * 1024) {
            snprintf(size_str, sizeof(size_str), "%.1f MB", entry->size / (1024.0 * 1024.0));
        } else {
            snprintf(size_str, sizeof(size_str), "%.1f GB", entry->size / (1024.0 * 1024.0 * 1024.0));
        }

        // Format modified time
        char date_str[32];
        struct tm *tm_info = localtime(&entry->modified);
        strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M", tm_info);

        snprintf(message, sizeof(message),
                "Name: %s\nSize: %s\nModified: %s\nType: %s",
                entry->name, size_str, date_str,
                entry->is_directory ? "Folder" : (entry->extension[0] ? entry->extension : "File"));

        dialog_info(&app->dialog, "Info", message);
    } else {
        // Empty space - show directory info
        snprintf(message, sizeof(message),
                "Directory: %s\nItems: %d",
                app->directory.current_path, app->directory.count);
        dialog_info(&app->dialog, "Info", message);
    }
}

static void action_new_folder(struct App *app)
{
    char new_folder_path[PATH_MAX_LEN];
    snprintf(new_folder_path, sizeof(new_folder_path), "%s/untitled folder",
             app->directory.current_path);

    // Find a unique name
    char unique_path[PATH_MAX_LEN];
    safe_strcpy(unique_path, new_folder_path, sizeof(unique_path));

    int counter = 1;
    struct stat st;
    while (stat(unique_path, &st) == 0) {
        snprintf(unique_path, sizeof(unique_path), "%s/untitled folder %d",
                 app->directory.current_path, counter++);
    }

    const char *new_name = strrchr(unique_path, '/');
    new_name = new_name ? new_name + 1 : unique_path;

    OperationResult result = file_create_directory(app->directory.current_path, new_name);
    if (result == OP_SUCCESS) {
        directory_read(&app->directory, app->directory.current_path);

        // Find and select the new folder, enter rename mode
        for (int i = 0; i < app->directory.count; i++) {
            if (strcmp(app->directory.entries[i].name, new_name) == 0) {
                app->selected_index = i;
                selection_clear(&app->selection);

                // Enter rename mode
                app->rename_mode = true;
                app->rename_index = i;
                safe_strcpy(app->rename_buffer, new_name, NAME_MAX_LEN);
                app->rename_cursor = (int)strlen(app->rename_buffer);
                break;
            }
        }
    }
}

// ============================================================
// AI Action Callbacks
// ============================================================

static void action_edit_image_ai(struct App *app)
{
    ContextMenuState *menu = &app->context_menu;
    if (menu->target_path[0] == '\0') {
        return;
    }

    // Load the image into preview panel
    preview_load(&app->preview, menu->target_path);

    // Make preview visible if hidden
    if (!app->preview.visible) {
        app->preview.visible = true;
    }

    // Enter image edit input mode
    app->preview.edit_state = IMAGE_EDIT_INPUT;
    app->preview.edit_buffer[0] = '\0';
    app->preview.edit_cursor = 0;
    app->preview.edit_error[0] = '\0';
}

static void action_summarize(struct App *app)
{
    ContextMenuState *menu = &app->context_menu;
    if (menu->target_path[0] == '\0') {
        return;
    }

    // Start async summarization
    // The summary will be displayed in a dialog when complete
    safe_strcpy(app->async_summary_request.file_path, menu->target_path,
                sizeof(app->async_summary_request.file_path));
    app->async_summary_request.from_context_menu = true;  // Flag to show dialog on completion

    summarize_async_start(&app->async_summary_request, &app->summary_thread,
                          menu->target_path, &app->summary_config, app->summary_cache);
}

static void action_edit_text_ai(struct App *app)
{
    ContextMenuState *menu = &app->context_menu;
    if (menu->target_path[0] == '\0') {
        return;
    }

    // Set up text editing state
    app->text_edit_state = TEXT_EDIT_INPUT;
    safe_strcpy(app->text_edit_path, menu->target_path, sizeof(app->text_edit_path));
    app->text_edit_prompt[0] = '\0';
    app->text_edit_cursor = 0;
    app->text_edit_error[0] = '\0';
}
#endif // TESTING

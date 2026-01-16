#include "breadcrumb.h"
#include "sidebar.h"
#include "tabs.h"
#include "../app.h"
#include "../core/filesystem.h"
#include "../core/search.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BREADCRUMB_PADDING 8
#define BREADCRUMB_SEPARATOR_WIDTH 16

void breadcrumb_init(BreadcrumbState *breadcrumb)
{
    memset(breadcrumb, 0, sizeof(BreadcrumbState));
    breadcrumb->count = 0;
    breadcrumb->hovered = -1;
    breadcrumb->editing = false;
    breadcrumb->edit_buffer[0] = '\0';
    breadcrumb->edit_cursor = 0;
}

void breadcrumb_update(BreadcrumbState *breadcrumb, const char *path)
{
    breadcrumb->count = 0;
    breadcrumb->hovered = -1;

    if (!path || path[0] == '\0') return;

    // Split path into segments
    char temp_path[4096];
    strncpy(temp_path, path, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';

    // Build segments from path
    char *token;
    char *rest = temp_path;
    char current_path[4096] = "";

    // Handle root
    if (path[0] == '/') {
        BreadcrumbSegment *seg = &breadcrumb->segments[breadcrumb->count];
        strncpy(seg->name, "/", sizeof(seg->name) - 1);
        strncpy(seg->path, "/", sizeof(seg->path) - 1);
        seg->x = 0;
        seg->width = 0;
        breadcrumb->count++;
        strncpy(current_path, "/", sizeof(current_path) - 1);
        rest++;  // Skip leading slash
    }

    while ((token = strtok_r(rest, "/", &rest)) != NULL && breadcrumb->count < BREADCRUMB_MAX_SEGMENTS) {
        if (token[0] == '\0') continue;

        BreadcrumbSegment *seg = &breadcrumb->segments[breadcrumb->count];

        strncpy(seg->name, token, sizeof(seg->name) - 1);
        seg->name[sizeof(seg->name) - 1] = '\0';

        // Build full path for this segment
        if (current_path[strlen(current_path) - 1] != '/') {
            strncat(current_path, "/", sizeof(current_path) - strlen(current_path) - 1);
        }
        strncat(current_path, token, sizeof(current_path) - strlen(current_path) - 1);

        strncpy(seg->path, current_path, sizeof(seg->path) - 1);
        seg->path[sizeof(seg->path) - 1] = '\0';

        seg->x = 0;
        seg->width = 0;

        breadcrumb->count++;
    }
}

int breadcrumb_get_height(void)
{
    return BREADCRUMB_HEIGHT;
}

bool breadcrumb_is_editing(BreadcrumbState *breadcrumb)
{
    return breadcrumb->editing;
}

void breadcrumb_handle_input(struct App *app)
{
    BreadcrumbState *breadcrumb = &app->breadcrumb;

    // Edit mode handling
    if (breadcrumb->editing) {
        // Escape to cancel
        if (IsKeyPressed(KEY_ESCAPE)) {
            breadcrumb->editing = false;
            return;
        }

        // Enter to navigate
        if (IsKeyPressed(KEY_ENTER)) {
            if (breadcrumb->edit_buffer[0] != '\0') {
                // Expand ~ to home
                char expanded[4096];
                if (breadcrumb->edit_buffer[0] == '~') {
                    const char *home = getenv("HOME");
                    if (home) {
                        snprintf(expanded, sizeof(expanded), "%s%s", home, breadcrumb->edit_buffer + 1);
                    } else {
                        strncpy(expanded, breadcrumb->edit_buffer, sizeof(expanded) - 1);
                    }
                } else {
                    strncpy(expanded, breadcrumb->edit_buffer, sizeof(expanded) - 1);
                }
                expanded[sizeof(expanded) - 1] = '\0';

                if (directory_read(&app->directory, expanded)) {
                    app->selected_index = 0;
                    app->scroll_offset = 0;
                    selection_clear(&app->selection);
                    history_push(&app->history, app->directory.current_path);
                    breadcrumb_update(breadcrumb, app->directory.current_path);
                }
            }
            breadcrumb->editing = false;
            return;
        }

        // Backspace
        if (IsKeyPressed(KEY_BACKSPACE)) {
            if (breadcrumb->edit_cursor > 0) {
                int len = strlen(breadcrumb->edit_buffer);
                for (int i = breadcrumb->edit_cursor - 1; i < len; i++) {
                    breadcrumb->edit_buffer[i] = breadcrumb->edit_buffer[i + 1];
                }
                breadcrumb->edit_cursor--;
            }
            return;
        }

        // Delete
        if (IsKeyPressed(KEY_DELETE)) {
            int len = strlen(breadcrumb->edit_buffer);
            if (breadcrumb->edit_cursor < len) {
                for (int i = breadcrumb->edit_cursor; i < len; i++) {
                    breadcrumb->edit_buffer[i] = breadcrumb->edit_buffer[i + 1];
                }
            }
            return;
        }

        // Cursor movement
        if (IsKeyPressed(KEY_LEFT)) {
            if (breadcrumb->edit_cursor > 0) breadcrumb->edit_cursor--;
            return;
        }
        if (IsKeyPressed(KEY_RIGHT)) {
            int len = strlen(breadcrumb->edit_buffer);
            if (breadcrumb->edit_cursor < len) breadcrumb->edit_cursor++;
            return;
        }
        if (IsKeyPressed(KEY_HOME)) {
            breadcrumb->edit_cursor = 0;
            return;
        }
        if (IsKeyPressed(KEY_END)) {
            breadcrumb->edit_cursor = strlen(breadcrumb->edit_buffer);
            return;
        }

        // Text input
        int key = GetCharPressed();
        while (key > 0) {
            if (key >= 32 && key <= 126) {
                int len = strlen(breadcrumb->edit_buffer);
                if (len < (int)sizeof(breadcrumb->edit_buffer) - 1) {
                    for (int i = len; i >= breadcrumb->edit_cursor; i--) {
                        breadcrumb->edit_buffer[i + 1] = breadcrumb->edit_buffer[i];
                    }
                    breadcrumb->edit_buffer[breadcrumb->edit_cursor] = (char)key;
                    breadcrumb->edit_cursor++;
                }
            }
            key = GetCharPressed();
        }

        return;
    }

    // Check for click on breadcrumb bar
    Vector2 mouse = GetMousePosition();
    int tab_height = tabs_get_height(&app->tabs);
    int search_height = search_is_active(&app->search) ? 32 : 0;
    int breadcrumb_y = tab_height + search_height;

    if (mouse.y >= breadcrumb_y && mouse.y < breadcrumb_y + BREADCRUMB_HEIGHT) {
        // Check if clicking on a segment
        breadcrumb->hovered = -1;
        for (int i = 0; i < breadcrumb->count; i++) {
            BreadcrumbSegment *seg = &breadcrumb->segments[i];
            if (mouse.x >= seg->x && mouse.x < seg->x + seg->width) {
                breadcrumb->hovered = i;

                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    // Navigate to this path
                    if (directory_read(&app->directory, seg->path)) {
                        app->selected_index = 0;
                        app->scroll_offset = 0;
                        selection_clear(&app->selection);
                        history_push(&app->history, app->directory.current_path);
                        breadcrumb_update(breadcrumb, app->directory.current_path);
                    }
                }
                break;
            }
        }

        // Double-click to enter edit mode
        // (simplified: use Cmd+L or click on empty area)
        if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) {
            if (IsKeyPressed(KEY_L)) {
                breadcrumb->editing = true;
                strncpy(breadcrumb->edit_buffer, app->directory.current_path, sizeof(breadcrumb->edit_buffer) - 1);
                breadcrumb->edit_buffer[sizeof(breadcrumb->edit_buffer) - 1] = '\0';
                breadcrumb->edit_cursor = strlen(breadcrumb->edit_buffer);
            }
        }
    } else {
        breadcrumb->hovered = -1;
    }
}

void breadcrumb_draw(struct App *app)
{
    BreadcrumbState *breadcrumb = &app->breadcrumb;

    int sidebar_width = app->sidebar.collapsed ? 0 : app->sidebar.width;
    int tab_height = tabs_get_height(&app->tabs);
    int search_height = search_is_active(&app->search) ? 32 : 0;
    int breadcrumb_y = tab_height + search_height;
    int content_width = app->width - sidebar_width;

    // Background
    DrawRectangle(sidebar_width, breadcrumb_y, content_width, BREADCRUMB_HEIGHT, g_theme.sidebar);
    DrawLine(sidebar_width, breadcrumb_y + BREADCRUMB_HEIGHT - 1, app->width, breadcrumb_y + BREADCRUMB_HEIGHT - 1, g_theme.border);

    if (breadcrumb->editing) {
        // Edit mode - show text input
        int text_x = sidebar_width + BREADCRUMB_PADDING;
        int text_y = breadcrumb_y + (BREADCRUMB_HEIGHT - FONT_SIZE) / 2;

        DrawTextCustom(breadcrumb->edit_buffer, text_x, text_y, FONT_SIZE, g_theme.textPrimary);

        // Cursor
        float blink = GetTime();
        if ((int)(blink * 2) % 2 == 0) {
            char temp[4096];
            strncpy(temp, breadcrumb->edit_buffer, breadcrumb->edit_cursor);
            temp[breadcrumb->edit_cursor] = '\0';
            int cursor_x = text_x + MeasureTextCustom(temp, FONT_SIZE);
            DrawRectangle(cursor_x, text_y, 2, FONT_SIZE, g_theme.accent);
        }
        return;
    }

    // Draw path segments
    int x = sidebar_width + BREADCRUMB_PADDING;

    for (int i = 0; i < breadcrumb->count; i++) {
        BreadcrumbSegment *seg = &breadcrumb->segments[i];

        int text_width = MeasureTextCustom(seg->name, FONT_SIZE_SMALL);
        seg->x = x;
        seg->width = text_width + BREADCRUMB_PADDING * 2;

        // Hover highlight
        if (i == breadcrumb->hovered) {
            DrawRectangle(x, breadcrumb_y + 4, seg->width, BREADCRUMB_HEIGHT - 8, g_theme.hover);
        }

        // Text
        Color text_color = (i == breadcrumb->hovered) ? g_theme.accent : g_theme.textSecondary;
        int text_y = breadcrumb_y + (BREADCRUMB_HEIGHT - FONT_SIZE_SMALL) / 2;
        DrawTextCustom(seg->name, x + BREADCRUMB_PADDING, text_y, FONT_SIZE_SMALL, text_color);

        x += seg->width;

        // Separator (except for last)
        if (i < breadcrumb->count - 1) {
            DrawTextCustom(">", x, text_y, FONT_SIZE_SMALL, g_theme.textSecondary);
            x += BREADCRUMB_SEPARATOR_WIDTH;
        }

        // Truncate if too wide
        if (x > sidebar_width + content_width - 50) {
            DrawTextCustom("...", x, text_y, FONT_SIZE_SMALL, g_theme.textSecondary);
            break;
        }
    }
}

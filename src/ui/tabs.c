#include "tabs.h"
#include "../app.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>
#include <libgen.h>

void tabs_init(TabState *tabs)
{
    memset(tabs, 0, sizeof(TabState));
    tabs->count = 0;
    tabs->current = -1;
    tabs->hovered = -1;
    tabs->close_hovered = false;
    tabs->dragging = false;
    tabs->drag_index = -1;
}

void tabs_update_title(Tab *tab)
{
    if (!tab->active) return;

    // Extract folder name from path
    char temp[PATH_MAX_LEN];
    strncpy(temp, tab->path, PATH_MAX_LEN - 1);
    temp[PATH_MAX_LEN - 1] = '\0';

    char *name = basename(temp);
    if (name && name[0] != '\0') {
        strncpy(tab->title, name, sizeof(tab->title) - 1);
        tab->title[sizeof(tab->title) - 1] = '\0';
    } else {
        strncpy(tab->title, "/", sizeof(tab->title) - 1);
    }
}

int tabs_new(TabState *tabs, const char *path)
{
    if (tabs->count >= MAX_TABS) {
        return -1;
    }

    // Find first inactive slot
    int index = -1;
    for (int i = 0; i < MAX_TABS; i++) {
        if (!tabs->tabs[i].active) {
            index = i;
            break;
        }
    }

    if (index < 0) return -1;

    Tab *tab = &tabs->tabs[index];
    tab->active = true;
    strncpy(tab->path, path, PATH_MAX_LEN - 1);
    tab->path[PATH_MAX_LEN - 1] = '\0';
    tab->selected_index = 0;
    tab->scroll_offset = 0;

    tabs_update_title(tab);

    tabs->count++;
    tabs->current = index;

    return index;
}

void tabs_close(TabState *tabs, int index)
{
    if (index < 0 || index >= MAX_TABS) return;
    if (!tabs->tabs[index].active) return;
    if (tabs->count <= 1) return; // Keep at least one tab

    tabs->tabs[index].active = false;
    tabs->count--;

    // If we closed the current tab, switch to another
    if (tabs->current == index) {
        // Find next active tab
        for (int i = index + 1; i < MAX_TABS; i++) {
            if (tabs->tabs[i].active) {
                tabs->current = i;
                return;
            }
        }
        // Find previous active tab
        for (int i = index - 1; i >= 0; i--) {
            if (tabs->tabs[i].active) {
                tabs->current = i;
                return;
            }
        }
    }
}

void tabs_switch(TabState *tabs, int index)
{
    if (index < 0 || index >= MAX_TABS) return;
    if (!tabs->tabs[index].active) return;
    tabs->current = index;
}

void tabs_next(TabState *tabs)
{
    if (tabs->count <= 1) return;

    // Find next active tab
    for (int i = tabs->current + 1; i < MAX_TABS; i++) {
        if (tabs->tabs[i].active) {
            tabs->current = i;
            return;
        }
    }
    // Wrap around
    for (int i = 0; i < tabs->current; i++) {
        if (tabs->tabs[i].active) {
            tabs->current = i;
            return;
        }
    }
}

void tabs_prev(TabState *tabs)
{
    if (tabs->count <= 1) return;

    // Find previous active tab
    for (int i = tabs->current - 1; i >= 0; i--) {
        if (tabs->tabs[i].active) {
            tabs->current = i;
            return;
        }
    }
    // Wrap around
    for (int i = MAX_TABS - 1; i > tabs->current; i--) {
        if (tabs->tabs[i].active) {
            tabs->current = i;
            return;
        }
    }
}

void tabs_sync_from_app(TabState *tabs, struct App *app)
{
    if (tabs->current < 0 || tabs->current >= MAX_TABS) return;
    if (!tabs->tabs[tabs->current].active) return;

    Tab *tab = &tabs->tabs[tabs->current];
    strncpy(tab->path, app->directory.current_path, PATH_MAX_LEN - 1);
    tab->path[PATH_MAX_LEN - 1] = '\0';
    tab->selected_index = app->selected_index;
    tab->scroll_offset = app->scroll_offset;
    tabs_update_title(tab);
}

void tabs_sync_to_app(TabState *tabs, struct App *app)
{
    if (tabs->current < 0 || tabs->current >= MAX_TABS) return;
    if (!tabs->tabs[tabs->current].active) return;

    Tab *tab = &tabs->tabs[tabs->current];

    // Only reload if path changed
    if (strcmp(app->directory.current_path, tab->path) != 0) {
        directory_read(&app->directory, tab->path);
    }

    app->selected_index = tab->selected_index;
    app->scroll_offset = tab->scroll_offset;

    // Clamp selection to valid range
    if (app->selected_index >= app->directory.count) {
        app->selected_index = app->directory.count > 0 ? app->directory.count - 1 : 0;
    }
}

int tabs_get_height(TabState *tabs)
{
    return (tabs->count > 0) ? TAB_HEIGHT : 0;
}

void tabs_handle_input(struct App *app)
{
    TabState *tabs = &app->tabs;
    bool cmd_down = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    bool shift_down = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    // New tab: Cmd+T
    if (cmd_down && IsKeyPressed(KEY_T)) {
        // Save current tab state first
        tabs_sync_from_app(tabs, app);
        // Create new tab with current path
        tabs_new(tabs, app->directory.current_path);
    }

    // Close tab: Cmd+W
    if (cmd_down && IsKeyPressed(KEY_W)) {
        if (tabs->count > 1) {
            tabs_sync_from_app(tabs, app);
            tabs_close(tabs, tabs->current);
            tabs_sync_to_app(tabs, app);
        }
    }

    // Next tab: Cmd+Shift+]
    if (cmd_down && shift_down && IsKeyPressed(KEY_RIGHT_BRACKET)) {
        tabs_sync_from_app(tabs, app);
        tabs_next(tabs);
        tabs_sync_to_app(tabs, app);
    }

    // Previous tab: Cmd+Shift+[
    if (cmd_down && shift_down && IsKeyPressed(KEY_LEFT_BRACKET)) {
        tabs_sync_from_app(tabs, app);
        tabs_prev(tabs);
        tabs_sync_to_app(tabs, app);
    }

    // Switch to tab 1-9
    if (cmd_down && !shift_down) {
        for (int i = 0; i < 9; i++) {
            if (IsKeyPressed(KEY_ONE + i)) {
                // Find nth active tab
                int nth = 0;
                for (int j = 0; j < MAX_TABS; j++) {
                    if (tabs->tabs[j].active) {
                        if (nth == i) {
                            tabs_sync_from_app(tabs, app);
                            tabs_switch(tabs, j);
                            tabs_sync_to_app(tabs, app);
                            break;
                        }
                        nth++;
                    }
                }
            }
        }
    }

    // Handle mouse clicks on tabs
    Vector2 mouse = GetMousePosition();
    int sidebar_width = app->sidebar.collapsed ? 0 : app->sidebar.width;
    int tab_bar_y = 0;
    int tab_bar_height = tabs_get_height(tabs);

    if (tab_bar_height > 0 && mouse.y >= tab_bar_y && mouse.y < tab_bar_y + tab_bar_height) {
        int x = sidebar_width + PADDING;
        int tab_width = TAB_MAX_WIDTH;
        int available_width = app->width - sidebar_width - PADDING * 2;

        // Calculate tab width based on count
        if (tabs->count > 0) {
            tab_width = available_width / tabs->count;
            if (tab_width > TAB_MAX_WIDTH) tab_width = TAB_MAX_WIDTH;
            if (tab_width < TAB_MIN_WIDTH) tab_width = TAB_MIN_WIDTH;
        }

        tabs->hovered = -1;
        tabs->close_hovered = false;

        int tab_index = 0;
        for (int i = 0; i < MAX_TABS && tab_index < tabs->count; i++) {
            if (!tabs->tabs[i].active) continue;

            Rectangle tab_rect = { (float)x, (float)tab_bar_y, (float)tab_width, (float)tab_bar_height };

            if (CheckCollisionPointRec(mouse, tab_rect)) {
                tabs->hovered = i;

                // Check close button
                Rectangle close_rect = {
                    (float)(x + tab_width - TAB_CLOSE_SIZE - 4),
                    (float)(tab_bar_y + (tab_bar_height - TAB_CLOSE_SIZE) / 2),
                    (float)TAB_CLOSE_SIZE,
                    (float)TAB_CLOSE_SIZE
                };

                if (tabs->count > 1 && CheckCollisionPointRec(mouse, close_rect)) {
                    tabs->close_hovered = true;

                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        tabs_sync_from_app(tabs, app);
                        tabs_close(tabs, i);
                        tabs_sync_to_app(tabs, app);
                        return;
                    }
                } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    tabs_sync_from_app(tabs, app);
                    tabs_switch(tabs, i);
                    tabs_sync_to_app(tabs, app);
                }

                break;
            }

            x += tab_width + 2;
            tab_index++;
        }
    }
}

void tabs_draw(struct App *app)
{
    TabState *tabs = &app->tabs;

    if (tabs->count == 0) return;

    int sidebar_width = app->sidebar.collapsed ? 0 : app->sidebar.width;
    int tab_bar_y = 0;
    int tab_bar_height = tabs_get_height(tabs);

    // Draw tab bar background
    DrawRectangle(sidebar_width, tab_bar_y, app->width - sidebar_width, tab_bar_height, g_theme.sidebar);
    DrawLine(sidebar_width, tab_bar_height - 1, app->width, tab_bar_height - 1, g_theme.border);

    int x = sidebar_width + PADDING;
    int available_width = app->width - sidebar_width - PADDING * 2 - 30; // Reserve space for + button
    int tab_width = TAB_MAX_WIDTH;

    if (tabs->count > 0) {
        tab_width = available_width / tabs->count;
        if (tab_width > TAB_MAX_WIDTH) tab_width = TAB_MAX_WIDTH;
        if (tab_width < TAB_MIN_WIDTH) tab_width = TAB_MIN_WIDTH;
    }

    int tab_index = 0;
    for (int i = 0; i < MAX_TABS && tab_index < tabs->count; i++) {
        if (!tabs->tabs[i].active) continue;

        Tab *tab = &tabs->tabs[i];
        bool is_current = (i == tabs->current);
        bool is_hovered = (i == tabs->hovered);

        // Tab background
        Color bg_color = g_theme.sidebar;
        if (is_current) {
            bg_color = g_theme.background;
        } else if (is_hovered) {
            bg_color = g_theme.hover;
        }

        Rectangle tab_rect = { (float)x, (float)(tab_bar_y + 2), (float)(tab_width - 2), (float)(tab_bar_height - 2) };
        DrawRectangleRec(tab_rect, bg_color);

        // Tab title
        int text_x = x + PADDING;
        int text_y = tab_bar_y + (tab_bar_height - FONT_SIZE) / 2;
        int max_text_width = tab_width - PADDING * 2 - (tabs->count > 1 ? TAB_CLOSE_SIZE + 4 : 0);

        // Truncate title if too long
        char display_title[64];
        strncpy(display_title, tab->title, sizeof(display_title) - 1);
        display_title[sizeof(display_title) - 1] = '\0';

        int text_width = MeasureTextCustom(display_title, FONT_SIZE);
        while (text_width > max_text_width && strlen(display_title) > 3) {
            display_title[strlen(display_title) - 4] = '.';
            display_title[strlen(display_title) - 3] = '.';
            display_title[strlen(display_title) - 2] = '.';
            display_title[strlen(display_title) - 1] = '\0';
            text_width = MeasureTextCustom(display_title, FONT_SIZE);
        }

        DrawTextCustom(display_title, text_x, text_y, FONT_SIZE, is_current ? g_theme.textPrimary : g_theme.textSecondary);

        // Close button (only if more than one tab)
        if (tabs->count > 1) {
            int close_x = x + tab_width - TAB_CLOSE_SIZE - 8;
            int close_y = tab_bar_y + (tab_bar_height - TAB_CLOSE_SIZE) / 2;

            if (is_hovered && tabs->close_hovered) {
                DrawRectangle(close_x - 2, close_y - 2, TAB_CLOSE_SIZE + 4, TAB_CLOSE_SIZE + 4, g_theme.selection);
            }

            Color close_color = (is_hovered && tabs->close_hovered) ? g_theme.textPrimary : g_theme.textSecondary;

            // Draw X
            DrawLine(close_x + 3, close_y + 3, close_x + TAB_CLOSE_SIZE - 3, close_y + TAB_CLOSE_SIZE - 3, close_color);
            DrawLine(close_x + TAB_CLOSE_SIZE - 3, close_y + 3, close_x + 3, close_y + TAB_CLOSE_SIZE - 3, close_color);
        }

        // Separator line on right
        DrawLine(x + tab_width - 1, tab_bar_y + 4, x + tab_width - 1, tab_bar_y + tab_bar_height - 4, g_theme.border);

        x += tab_width;
        tab_index++;
    }

    // Draw + button for new tab
    Vector2 mouse = GetMousePosition();
    Rectangle plus_rect = { (float)x + 4, (float)(tab_bar_y + 4), (float)(tab_bar_height - 8), (float)(tab_bar_height - 8) };
    bool plus_hovered = CheckCollisionPointRec(mouse, plus_rect);

    if (plus_hovered) {
        DrawRectangleRec(plus_rect, g_theme.hover);
    }

    int plus_center_x = x + 4 + (tab_bar_height - 8) / 2;
    int plus_center_y = tab_bar_y + tab_bar_height / 2;
    Color plus_color = plus_hovered ? g_theme.textPrimary : g_theme.textSecondary;

    DrawLine(plus_center_x - 5, plus_center_y, plus_center_x + 5, plus_center_y, plus_color);
    DrawLine(plus_center_x, plus_center_y - 5, plus_center_x, plus_center_y + 5, plus_color);

    if (plus_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        tabs_sync_from_app(tabs, app);
        tabs_new(tabs, app->directory.current_path);
    }
}

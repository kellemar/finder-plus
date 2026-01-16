#include "sidebar.h"
#include "../app.h"
#include "../core/filesystem.h"
#include "../core/network.h"
#include "../utils/theme.h"
#include "../utils/font.h"

#include <stdio.h>
#include <string.h>

#define SECTION_HEADER_HEIGHT 24
#define SIDEBAR_ITEM_HEIGHT 22
#define RESIZE_HANDLE_WIDTH 4

// Draw a sidebar item
static void draw_sidebar_item(int x, int y, int width, const char *name, bool is_volume, bool is_selected, bool is_hovered)
{
    // Background for hover/selection
    if (is_selected) {
        DrawRectangle(x, y, width, SIDEBAR_ITEM_HEIGHT, g_theme.selection);
    } else if (is_hovered) {
        DrawRectangle(x, y, width, SIDEBAR_ITEM_HEIGHT, g_theme.hover);
    }

    // Icon
    const char *icon = is_volume ? "[V]" : "[*]";
    Color icon_color = is_volume ? g_theme.accent : g_theme.folder;
    DrawTextCustom(icon, x + PADDING, y + (SIDEBAR_ITEM_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, icon_color);

    // Name
    int text_x = x + PADDING + 25;
    int max_width = width - PADDING * 2 - 25;

    // Truncate if needed
    char display_name[64];
    int name_len = strlen(name);
    int max_chars = max_width / 7; // Approximate char width

    if (name_len > max_chars && max_chars > 3) {
        strncpy(display_name, name, max_chars - 3);
        display_name[max_chars - 3] = '\0';
        strcat(display_name, "...");
    } else {
        strncpy(display_name, name, sizeof(display_name) - 1);
        display_name[sizeof(display_name) - 1] = '\0';
    }

    DrawTextCustom(display_name, text_x, y + (SIDEBAR_ITEM_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, g_theme.textPrimary);
}

// Draw a network location item
static void draw_network_item(int x, int y, int width, const char *name, ConnectionStatus status, bool is_hovered)
{
    // Background for hover
    if (is_hovered) {
        DrawRectangle(x, y, width, SIDEBAR_ITEM_HEIGHT, g_theme.hover);
    }

    // Icon based on connection status
    const char *icon = "[N]";
    Color icon_color = g_theme.textSecondary;

    switch (status) {
        case CONN_STATUS_CONNECTED:
            icon = "[+]";
            icon_color = g_theme.success;
            break;
        case CONN_STATUS_CONNECTING:
        case CONN_STATUS_RECONNECTING:
            icon = "[~]";
            icon_color = g_theme.warning;
            break;
        case CONN_STATUS_ERROR:
            icon = "[!]";
            icon_color = g_theme.error;
            break;
        default:
            icon = "[N]";
            icon_color = g_theme.textSecondary;
            break;
    }

    DrawTextCustom(icon, x + PADDING, y + (SIDEBAR_ITEM_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, icon_color);

    // Name
    int text_x = x + PADDING + 25;
    int max_width = width - PADDING * 2 - 25;

    char display_name[64];
    int name_len = strlen(name);
    int max_chars = max_width / 7;

    if (name_len > max_chars && max_chars > 3) {
        strncpy(display_name, name, max_chars - 3);
        display_name[max_chars - 3] = '\0';
        strcat(display_name, "...");
    } else {
        strncpy(display_name, name, sizeof(display_name) - 1);
        display_name[sizeof(display_name) - 1] = '\0';
    }

    Color text_color = (status == CONN_STATUS_CONNECTED) ? g_theme.textPrimary : g_theme.textSecondary;
    DrawTextCustom(display_name, text_x, y + (SIDEBAR_ITEM_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, text_color);
}

void sidebar_draw(App *app)
{
    if (app->sidebar.collapsed) {
        return;
    }

    int width = app->sidebar.width;
    int y = 0;

    // Background
    DrawRectangle(0, 0, width, app->height - STATUSBAR_HEIGHT, g_theme.sidebar);

    // Right border
    DrawLine(width - 1, 0, width - 1, app->height - STATUSBAR_HEIGHT, g_theme.border);

    // Resize handle (visual feedback when near edge)
    if (app->sidebar.resizing) {
        DrawRectangle(width - RESIZE_HANDLE_WIDTH, 0, RESIZE_HANDLE_WIDTH, app->height - STATUSBAR_HEIGHT,
                      Fade(g_theme.accent, 0.5f));
    }

    // Favorites section
    DrawTextCustom("Favorites", PADDING, y + (SECTION_HEADER_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, g_theme.textSecondary);
    y += SECTION_HEADER_HEIGHT;

    for (int i = 0; i < app->sidebar.favorite_count; i++) {
        bool is_hovered = (app->sidebar.hovered_index == i);
        bool is_selected = (strcmp(app->directory.current_path, app->sidebar.favorites[i].path) == 0);

        draw_sidebar_item(0, y, width - RESIZE_HANDLE_WIDTH,
                          app->sidebar.favorites[i].name,
                          false, is_selected, is_hovered);
        y += SIDEBAR_ITEM_HEIGHT;
    }

    // Spacing
    y += PADDING * 2;

    // Locations section
    DrawTextCustom("Locations", PADDING, y + (SECTION_HEADER_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, g_theme.textSecondary);
    y += SECTION_HEADER_HEIGHT;

    for (int i = 0; i < app->sidebar.volume_count; i++) {
        bool is_hovered = (app->sidebar.hovered_index == 100 + i);
        bool is_selected = (strcmp(app->directory.current_path, app->sidebar.volumes[i].path) == 0);

        draw_sidebar_item(0, y, width - RESIZE_HANDLE_WIDTH,
                          app->sidebar.volumes[i].name,
                          true, is_selected, is_hovered);
        y += SIDEBAR_ITEM_HEIGHT;
    }

    // Network section (only show if there are saved profiles or active connections)
    if (app->network.profile_count > 0 || app->network.connection_count > 0) {
        // Spacing
        y += PADDING * 2;

        // Network section header
        DrawTextCustom("Network", PADDING, y + (SECTION_HEADER_HEIGHT - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, g_theme.textSecondary);
        y += SECTION_HEADER_HEIGHT;

        // Draw saved profiles
        for (int i = 0; i < app->network.profile_count; i++) {
            ConnectionProfile *profile = &app->network.saved_profiles[i];
            bool is_hovered = (app->sidebar.hovered_index == 200 + i);

            // Check if this profile has an active connection
            ConnectionStatus status = CONN_STATUS_DISCONNECTED;
            for (int j = 0; j < app->network.connection_count; j++) {
                NetworkConnection *conn = &app->network.connections[j];
                if (strcmp(conn->profile.host, profile->host) == 0 &&
                    strcmp(conn->profile.username, profile->username) == 0) {
                    status = conn->status;
                    break;
                }
            }

            draw_network_item(0, y, width - RESIZE_HANDLE_WIDTH, profile->name, status, is_hovered);
            y += SIDEBAR_ITEM_HEIGHT;
        }
    }
}

void sidebar_handle_input(App *app)
{
    if (app->sidebar.collapsed) {
        return;
    }

    Vector2 mouse = GetMousePosition();
    int width = app->sidebar.width;

    // Handle resize
    bool near_edge = (mouse.x >= width - RESIZE_HANDLE_WIDTH && mouse.x <= width + RESIZE_HANDLE_WIDTH);

    if (near_edge) {
        SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && near_edge) {
        app->sidebar.resizing = true;
    }

    if (app->sidebar.resizing) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            int new_width = (int)mouse.x;
            if (new_width < SIDEBAR_MIN_WIDTH) new_width = SIDEBAR_MIN_WIDTH;
            if (new_width > SIDEBAR_MAX_WIDTH) new_width = SIDEBAR_MAX_WIDTH;
            app->sidebar.width = new_width;
        } else {
            app->sidebar.resizing = false;
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }
        return; // Don't process clicks while resizing
    }

    // Reset cursor if not near edge
    if (!near_edge && !app->sidebar.resizing) {
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    }

    // Check if mouse is in sidebar area
    if (mouse.x >= width || mouse.y >= app->height - STATUSBAR_HEIGHT) {
        app->sidebar.hovered_index = -1;
        return;
    }

    // Calculate hovered item
    int y = SECTION_HEADER_HEIGHT; // Start after "Favorites" header
    app->sidebar.hovered_index = -1;

    // Check favorites
    for (int i = 0; i < app->sidebar.favorite_count; i++) {
        if (mouse.y >= y && mouse.y < y + SIDEBAR_ITEM_HEIGHT) {
            app->sidebar.hovered_index = i;

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                // Navigate to this favorite
                if (directory_read(&app->directory, app->sidebar.favorites[i].path)) {
                    app->selected_index = 0;
                    app->scroll_offset = 0;
                    selection_clear(&app->selection);
                    history_push(&app->history, app->directory.current_path);
                }
            }
            return;
        }
        y += SIDEBAR_ITEM_HEIGHT;
    }

    y += PADDING * 2 + SECTION_HEADER_HEIGHT; // Skip spacing and "Locations" header

    // Check volumes
    for (int i = 0; i < app->sidebar.volume_count; i++) {
        if (mouse.y >= y && mouse.y < y + SIDEBAR_ITEM_HEIGHT) {
            app->sidebar.hovered_index = 100 + i;

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                // Navigate to this volume
                if (directory_read(&app->directory, app->sidebar.volumes[i].path)) {
                    app->selected_index = 0;
                    app->scroll_offset = 0;
                    selection_clear(&app->selection);
                    history_push(&app->history, app->directory.current_path);
                }
            }
            return;
        }
        y += SIDEBAR_ITEM_HEIGHT;
    }

    // Check network profiles
    if (app->network.profile_count > 0 || app->network.connection_count > 0) {
        y += PADDING * 2 + SECTION_HEADER_HEIGHT; // Skip spacing and "Network" header

        for (int i = 0; i < app->network.profile_count; i++) {
            if (mouse.y >= y && mouse.y < y + SIDEBAR_ITEM_HEIGHT) {
                app->sidebar.hovered_index = 200 + i;

                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    // Connect to this network location
                    ConnectionProfile *profile = &app->network.saved_profiles[i];
                    int conn_id = network_connect(&app->network, profile);
                    if (conn_id >= 0) {
                        network_set_active(&app->network, conn_id);
                        // When connection is established, read the remote directory
                        // (In a full implementation, this would be async)
                    }
                }
                return;
            }
            y += SIDEBAR_ITEM_HEIGHT;
        }
    }
}

int sidebar_get_content_x(App *app)
{
    if (app->sidebar.collapsed) {
        return 0;
    }
    return app->sidebar.width;
}

int sidebar_get_content_width(App *app)
{
    if (app->sidebar.collapsed) {
        return app->width;
    }
    return app->width - app->sidebar.width;
}

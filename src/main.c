#include "raylib.h"
#include "app.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    // Parse command line arguments
    const char *start_path = NULL;
    if (argc > 1) {
        start_path = argv[1];
    }

    // Initialize window
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, APP_NAME);
    SetTargetFPS(60);
    SetExitKey(KEY_NULL); // Disable ESC to close

    // Initialize application state
    App app = {0};
    app_init(&app, start_path);

    // Update window title with current path
    char title[512];
    snprintf(title, sizeof(title), "%s - %s", APP_NAME, app.directory.current_path);
    SetWindowTitle(title);

    // Main loop
    while (!WindowShouldClose() && !app.should_close) {
        // Handle window resize
        if (IsWindowResized()) {
            app.width = GetScreenWidth();
            app.height = GetScreenHeight();
        }

        // Update window title when path changes
        static char last_path[4096] = {0};
        if (strcmp(last_path, app.directory.current_path) != 0) {
            snprintf(title, sizeof(title), "%s - %s", APP_NAME, app.directory.current_path);
            SetWindowTitle(title);
            strncpy(last_path, app.directory.current_path, sizeof(last_path) - 1);
            last_path[sizeof(last_path) - 1] = '\0';
        }

        // Update
        app_update(&app);

        // Draw
        app_draw(&app);
    }

    // Cleanup
    app_free(&app);
    CloseWindow();

    return 0;
}

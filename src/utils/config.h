#ifndef CONFIG_H
#define CONFIG_H

#include "../utils/theme.h"
#include <stdbool.h>

#define CONFIG_PATH_MAX 4096

// Window configuration
typedef struct WindowConfig {
    int width;
    int height;
    int x;
    int y;
    bool fullscreen;
    bool remember_position;
} WindowConfig;

// View mode (matches ViewMode enum in app.h)
typedef enum ConfigViewMode {
    CONFIG_VIEW_LIST = 0,
    CONFIG_VIEW_GRID = 1,
    CONFIG_VIEW_COLUMN = 2
} ConfigViewMode;

// Appearance configuration
typedef struct AppearanceConfig {
    ThemeType theme;
    ConfigViewMode view_mode;
    int font_size;
    int icon_size;
    int sidebar_width;
    int preview_width;
    bool show_hidden_files;
} AppearanceConfig;

// Keyboard configuration
typedef struct KeyboardConfig {
    bool vim_mode;
    // Future: custom keybindings
} KeyboardConfig;

// Bookmark entry
typedef struct BookmarkConfig {
    char name[64];
    char path[CONFIG_PATH_MAX];
} BookmarkConfig;

// Bookmarks configuration
typedef struct BookmarksConfig {
    BookmarkConfig bookmarks[16];
    int count;
} BookmarksConfig;

// AI configuration
typedef struct AIConfig {
    bool enabled;
    char api_key[256];
    bool semantic_search;
    bool smart_rename;
} AIConfig;

// Main configuration
typedef struct Config {
    WindowConfig window;
    AppearanceConfig appearance;
    KeyboardConfig keyboard;
    BookmarksConfig bookmarks;
    AIConfig ai;

    // Internal
    char config_path[CONFIG_PATH_MAX];
    bool loaded;
    bool modified;
} Config;

// Global config
extern Config g_config;

// Initialize config with defaults
void config_init(Config *config);

// Load config from file
bool config_load(Config *config, const char *path);

// Save config to file
bool config_save(Config *config);

// Get default config path (~/.config/finder-plus/config.json)
const char* config_get_default_path(void);

// Apply config to app (call after loading)
void config_apply(Config *config);

// Update config from current app state
void config_update_from_app(Config *config);

#endif // CONFIG_H

#include "config.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

Config g_config;

// Simple JSON writer helpers
static void json_write_string(FILE *f, const char *key, const char *value, bool comma)
{
    fprintf(f, "  \"%s\": \"%s\"%s\n", key, value, comma ? "," : "");
}

static void json_write_int(FILE *f, const char *key, int value, bool comma)
{
    fprintf(f, "  \"%s\": %d%s\n", key, value, comma ? "," : "");
}

static void json_write_bool(FILE *f, const char *key, bool value, bool comma)
{
    fprintf(f, "  \"%s\": %s%s\n", key, value ? "true" : "false", comma ? "," : "");
}

// Simple JSON reading helpers
static char* json_find_key(const char *json, const char *key)
{
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);

    char *pos = strstr(json, search);
    if (!pos) return NULL;

    pos = strchr(pos, ':');
    if (!pos) return NULL;

    pos++; // Skip ':'
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;

    return pos;
}

static int json_read_int(const char *json, const char *key, int default_value)
{
    char *pos = json_find_key(json, key);
    if (!pos) return default_value;

    int value;
    if (sscanf(pos, "%d", &value) == 1) {
        return value;
    }
    return default_value;
}

static bool json_read_bool(const char *json, const char *key, bool default_value)
{
    char *pos = json_find_key(json, key);
    if (!pos) return default_value;

    if (strncmp(pos, "true", 4) == 0) return true;
    if (strncmp(pos, "false", 5) == 0) return false;
    return default_value;
}

static void json_read_string(const char *json, const char *key, char *out, int max_len, const char *default_value)
{
    char *pos = json_find_key(json, key);
    if (!pos || *pos != '"') {
        strncpy(out, default_value, max_len - 1);
        out[max_len - 1] = '\0';
        return;
    }

    pos++; // Skip opening quote
    char *end = strchr(pos, '"');
    if (!end) {
        strncpy(out, default_value, max_len - 1);
        out[max_len - 1] = '\0';
        return;
    }

    int len = end - pos;
    if (len >= max_len) len = max_len - 1;

    strncpy(out, pos, len);
    out[len] = '\0';
}

void config_init(Config *config)
{
    memset(config, 0, sizeof(Config));

    // Window defaults
    config->window.width = 1280;
    config->window.height = 720;
    config->window.x = -1;  // Center
    config->window.y = -1;
    config->window.fullscreen = false;
    config->window.remember_position = true;

    // Appearance defaults
    config->appearance.theme = THEME_DARK;
    config->appearance.view_mode = CONFIG_VIEW_LIST;
    config->appearance.font_size = 14;
    config->appearance.icon_size = 24;
    config->appearance.sidebar_width = 200;
    config->appearance.preview_width = 300;
    config->appearance.show_hidden_files = false;

    // Keyboard defaults
    config->keyboard.vim_mode = true;

    // Bookmarks defaults
    config->bookmarks.count = 0;

    // AI defaults
    config->ai.enabled = false;
    config->ai.api_key[0] = '\0';
    config->ai.semantic_search = false;
    config->ai.smart_rename = false;

    // Get default config path
    strncpy(config->config_path, config_get_default_path(), CONFIG_PATH_MAX - 1);
    config->config_path[CONFIG_PATH_MAX - 1] = '\0';

    config->loaded = false;
    config->modified = false;
}

const char* config_get_default_path(void)
{
    static char path[CONFIG_PATH_MAX];
    const char *home = getenv("HOME");
    if (!home) {
        return "./config.json";
    }
    snprintf(path, sizeof(path), "%s/.config/finder-plus/config.json", home);
    return path;
}

static bool ensure_config_dir(const char *config_path)
{
    // Extract directory from path
    char dir[CONFIG_PATH_MAX];
    strncpy(dir, config_path, CONFIG_PATH_MAX - 1);
    dir[CONFIG_PATH_MAX - 1] = '\0';

    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        return true;  // No directory component
    }

    // Create directory recursively
    char temp[CONFIG_PATH_MAX];
    char *p = NULL;

    snprintf(temp, sizeof(temp), "%s", dir);

    for (p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }

    if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
        return false;
    }

    return true;
}

bool config_load(Config *config, const char *path)
{
    if (!path) {
        path = config_get_default_path();
    }

    strncpy(config->config_path, path, CONFIG_PATH_MAX - 1);
    config->config_path[CONFIG_PATH_MAX - 1] = '\0';

    FILE *f = fopen(path, "r");
    if (!f) {
        config->loaded = false;
        return false;
    }

    // Read entire file
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return false;
    }

    size_t bytes_read = fread(content, 1, size, f);
    content[bytes_read] = '\0';
    fclose(f);

    // Parse JSON (simple flat structure)
    config->window.width = json_read_int(content, "window_width", config->window.width);
    config->window.height = json_read_int(content, "window_height", config->window.height);
    config->window.x = json_read_int(content, "window_x", config->window.x);
    config->window.y = json_read_int(content, "window_y", config->window.y);
    config->window.fullscreen = json_read_bool(content, "fullscreen", config->window.fullscreen);
    config->window.remember_position = json_read_bool(content, "remember_position", config->window.remember_position);

    int theme_int = json_read_int(content, "theme", config->appearance.theme);
    if (theme_int >= THEME_DARK && theme_int <= THEME_SYSTEM) {
        config->appearance.theme = (ThemeType)theme_int;
    }
    int view_mode_int = json_read_int(content, "view_mode", config->appearance.view_mode);
    if (view_mode_int >= CONFIG_VIEW_LIST && view_mode_int <= CONFIG_VIEW_COLUMN) {
        config->appearance.view_mode = (ConfigViewMode)view_mode_int;
    }
    config->appearance.font_size = json_read_int(content, "font_size", config->appearance.font_size);
    config->appearance.icon_size = json_read_int(content, "icon_size", config->appearance.icon_size);
    config->appearance.sidebar_width = json_read_int(content, "sidebar_width", config->appearance.sidebar_width);
    config->appearance.preview_width = json_read_int(content, "preview_width", config->appearance.preview_width);
    config->appearance.show_hidden_files = json_read_bool(content, "show_hidden_files", config->appearance.show_hidden_files);

    config->keyboard.vim_mode = json_read_bool(content, "vim_mode", config->keyboard.vim_mode);

    config->ai.enabled = json_read_bool(content, "ai_enabled", config->ai.enabled);
    json_read_string(content, "api_key", config->ai.api_key, sizeof(config->ai.api_key), "");
    config->ai.semantic_search = json_read_bool(content, "semantic_search", config->ai.semantic_search);
    config->ai.smart_rename = json_read_bool(content, "smart_rename", config->ai.smart_rename);

    free(content);
    config->loaded = true;
    config->modified = false;
    return true;
}

bool config_save(Config *config)
{
    if (!ensure_config_dir(config->config_path)) {
        return false;
    }

    FILE *f = fopen(config->config_path, "w");
    if (!f) {
        return false;
    }

    fprintf(f, "{\n");

    // Window
    json_write_int(f, "window_width", config->window.width, true);
    json_write_int(f, "window_height", config->window.height, true);
    json_write_int(f, "window_x", config->window.x, true);
    json_write_int(f, "window_y", config->window.y, true);
    json_write_bool(f, "fullscreen", config->window.fullscreen, true);
    json_write_bool(f, "remember_position", config->window.remember_position, true);

    // Appearance
    json_write_int(f, "theme", (int)config->appearance.theme, true);
    json_write_int(f, "view_mode", (int)config->appearance.view_mode, true);
    json_write_int(f, "font_size", config->appearance.font_size, true);
    json_write_int(f, "icon_size", config->appearance.icon_size, true);
    json_write_int(f, "sidebar_width", config->appearance.sidebar_width, true);
    json_write_int(f, "preview_width", config->appearance.preview_width, true);
    json_write_bool(f, "show_hidden_files", config->appearance.show_hidden_files, true);

    // Keyboard
    json_write_bool(f, "vim_mode", config->keyboard.vim_mode, true);

    // AI (don't save api_key for security)
    json_write_bool(f, "ai_enabled", config->ai.enabled, true);
    json_write_bool(f, "semantic_search", config->ai.semantic_search, true);
    json_write_bool(f, "smart_rename", config->ai.smart_rename, false);

    fprintf(f, "}\n");
    fclose(f);

    config->modified = false;
    return true;
}

void config_apply(Config *config)
{
    // Apply theme
    theme_set(config->appearance.theme);

    // Other settings are applied when reading config
    // in app initialization or update
}

void config_update_from_app(Config *config)
{
    // This would be called to update config from current app state
    // before saving (e.g., window position, sidebar width)
    config->appearance.theme = theme_get_current_type();
    config->modified = true;
}

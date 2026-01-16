#ifndef THEME_H
#define THEME_H

#include "raylib.h"
#include <stdbool.h>

// Theme type
typedef enum ThemeType {
    THEME_DARK,
    THEME_LIGHT,
    THEME_SYSTEM  // Auto-detect from system
} ThemeType;

// Theme colors (from SPEC.md)
typedef struct Theme {
    Color background;       // Main background
    Color sidebar;          // Sidebar background
    Color selection;        // Selected item background
    Color hover;            // Hover state
    Color textPrimary;      // Primary text
    Color textSecondary;    // Secondary text
    Color accent;           // Accent color
    Color aiAccent;         // AI feature accent
    Color folder;           // Folder icon color
    Color file;             // File icon color
    Color border;           // Border/separator
    Color error;            // Error state
    Color success;          // Success state
    Color warning;          // Warning state
    // Git status colors
    Color gitModified;      // Modified files
    Color gitStaged;        // Staged files
    Color gitUntracked;     // Untracked files
    Color gitConflict;      // Conflict files
    Color gitDeleted;       // Deleted files
} Theme;

// Get theme presets
Theme theme_get_dark(void);
Theme theme_get_light(void);

// Global theme (set in app initialization)
extern Theme g_theme;
extern ThemeType g_theme_type;

// Theme management
void theme_init(ThemeType type);
void theme_set(ThemeType type);
void theme_toggle(void);
ThemeType theme_get_current_type(void);
Theme *theme_get_current(void);
bool theme_is_dark(void);

// System appearance detection (macOS)
bool theme_system_is_dark(void);

// UI dimensions
#define STATUSBAR_HEIGHT 28
#define ROW_HEIGHT 24
#define FONT_SIZE 16
#define FONT_SIZE_SMALL 14
#define PADDING 8
#define ICON_WIDTH 20
#define SCROLLBAR_WIDTH 12

#endif // THEME_H

#include "theme.h"

#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_MAC
// Forward declaration for macOS dark mode detection
extern bool macos_is_dark_mode(void);
#endif
#endif

Theme g_theme;
ThemeType g_theme_type = THEME_DARK;

// Helper to create Color from hex
static Color color_from_hex(unsigned int hex)
{
    return (Color){
        .r = (hex >> 16) & 0xFF,
        .g = (hex >> 8) & 0xFF,
        .b = hex & 0xFF,
        .a = 255
    };
}

Theme theme_get_dark(void)
{
    return (Theme){
        .background = color_from_hex(0x1E1E2E),
        .sidebar = color_from_hex(0x181825),
        .selection = color_from_hex(0x45475A),
        .hover = color_from_hex(0x313244),
        .textPrimary = color_from_hex(0xCDD6F4),
        .textSecondary = color_from_hex(0xA6ADC8),
        .accent = color_from_hex(0x89B4FA),
        .aiAccent = color_from_hex(0xCBA6F7),
        .folder = color_from_hex(0xF9E2AF),
        .file = color_from_hex(0xCDD6F4),
        .border = color_from_hex(0x45475A),
        .error = color_from_hex(0xF38BA8),
        .success = color_from_hex(0xA6E3A1),
        .warning = color_from_hex(0xFAB387),
        // Git colors (from SPEC.md)
        .gitModified = color_from_hex(0xFAB387),   // Orange - Modified
        .gitStaged = color_from_hex(0xA6E3A1),    // Green - Staged
        .gitUntracked = color_from_hex(0x94E2D5), // Teal - Untracked
        .gitConflict = color_from_hex(0xF38BA8),  // Red - Conflict
        .gitDeleted = color_from_hex(0xF38BA8)    // Red - Deleted
    };
}

Theme theme_get_light(void)
{
    return (Theme){
        .background = color_from_hex(0xEFF1F5),
        .sidebar = color_from_hex(0xE6E9EF),
        .selection = color_from_hex(0xBCC0CC),
        .hover = color_from_hex(0xCCD0DA),
        .textPrimary = color_from_hex(0x4C4F69),
        .textSecondary = color_from_hex(0x6C6F85),
        .accent = color_from_hex(0x1E66F5),
        .aiAccent = color_from_hex(0x8839EF),
        .folder = color_from_hex(0xDF8E1D),
        .file = color_from_hex(0x4C4F69),
        .border = color_from_hex(0xBCC0CC),
        .error = color_from_hex(0xD20F39),
        .success = color_from_hex(0x40A02B),
        .warning = color_from_hex(0xFE640B),
        // Git colors (Catppuccin Latte)
        .gitModified = color_from_hex(0xFE640B),   // Orange - Modified
        .gitStaged = color_from_hex(0x40A02B),    // Green - Staged
        .gitUntracked = color_from_hex(0x179299), // Teal - Untracked
        .gitConflict = color_from_hex(0xD20F39),  // Red - Conflict
        .gitDeleted = color_from_hex(0xD20F39)    // Red - Deleted
    };
}

bool theme_system_is_dark(void)
{
#ifdef __APPLE__
#if TARGET_OS_MAC
    // Use Objective-C runtime to check system appearance
    // This is a simplified check - full implementation would use NSAppearance
    const char *mode = getenv("AppleInterfaceStyle");
    return (mode != NULL && strcmp(mode, "Dark") == 0);
#endif
#endif
    return true;  // Default to dark on non-macOS
}

void theme_init(ThemeType type)
{
    g_theme_type = type;
    theme_set(type);
}

void theme_set(ThemeType type)
{
    g_theme_type = type;

    switch (type) {
        case THEME_DARK:
            g_theme = theme_get_dark();
            break;
        case THEME_LIGHT:
            g_theme = theme_get_light();
            break;
        case THEME_SYSTEM:
            if (theme_system_is_dark()) {
                g_theme = theme_get_dark();
            } else {
                g_theme = theme_get_light();
            }
            break;
    }
}

void theme_toggle(void)
{
    if (g_theme_type == THEME_DARK || (g_theme_type == THEME_SYSTEM && theme_system_is_dark())) {
        theme_set(THEME_LIGHT);
    } else {
        theme_set(THEME_DARK);
    }
}

ThemeType theme_get_current_type(void)
{
    return g_theme_type;
}

Theme *theme_get_current(void)
{
    return &g_theme;
}

bool theme_is_dark(void)
{
    if (g_theme_type == THEME_SYSTEM) {
        return theme_system_is_dark();
    }
    return g_theme_type == THEME_DARK;
}

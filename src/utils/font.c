#include "font.h"
#include "theme.h"
#include <string.h>

Font g_font = {0};
Font g_font_small = {0};
static bool fonts_loaded = false;

bool font_init(const char *font_path) {
    if (!font_path || font_path[0] == '\0') {
        fonts_loaded = false;
        return false;
    }

    // Load fonts at higher resolution for sharper rendering
    // Using 2x size for better quality when displayed
    g_font = LoadFontEx(font_path, FONT_SIZE * 2, NULL, 0);
    if (g_font.texture.id == 0) {
        fonts_loaded = false;
        return false;
    }

    g_font_small = LoadFontEx(font_path, FONT_SIZE_SMALL * 2, NULL, 0);
    if (g_font_small.texture.id == 0) {
        UnloadFont(g_font);
        g_font = (Font){0};
        fonts_loaded = false;
        return false;
    }

    // Use bilinear filtering for smooth scaling from higher resolution
    SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(g_font_small.texture, TEXTURE_FILTER_BILINEAR);

    fonts_loaded = true;
    return true;
}

void font_free(void) {
    if (fonts_loaded) {
        UnloadFont(g_font);
        UnloadFont(g_font_small);
        g_font = (Font){0};
        g_font_small = (Font){0};
        fonts_loaded = false;
    }
}

bool font_is_loaded(void) {
    return fonts_loaded;
}

Font font_get(int fontSize) {
    if (!fonts_loaded) {
        return GetFontDefault();
    }
    return (fontSize <= FONT_SIZE_SMALL) ? g_font_small : g_font;
}

void DrawTextCustom(const char *text, int posX, int posY, int fontSize, Color color) {
    if (!text) return;

    if (!fonts_loaded) {
        DrawText(text, posX, posY, fontSize, color);
        return;
    }

    Font font = font_get(fontSize);
    Vector2 pos = { (float)posX, (float)posY };
    float spacing = 0.0f;  // Pixel-art fonts: no extra spacing
    DrawTextEx(font, text, pos, (float)fontSize, spacing, color);
}

int MeasureTextCustom(const char *text, int fontSize) {
    if (!text) return 0;

    if (!fonts_loaded) {
        return MeasureText(text, fontSize);
    }

    Font font = font_get(fontSize);
    Vector2 size = MeasureTextEx(font, text, (float)fontSize, 0.0f);
    return (int)size.x;
}

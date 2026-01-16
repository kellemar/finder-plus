#ifndef FONT_H
#define FONT_H

#include "raylib.h"
#include <stdbool.h>

// Global fonts - initialized in font_init(), freed in font_free()
extern Font g_font;        // Primary font (FONT_SIZE, typically 14)
extern Font g_font_small;  // Small font (FONT_SIZE_SMALL, typically 12)

// Initialize fonts from file
// Returns true if fonts loaded successfully, false uses default fallback
bool font_init(const char *font_path);

// Free font resources
void font_free(void);

// Check if custom fonts are loaded
bool font_is_loaded(void);

// Get the appropriate font for a given size
Font font_get(int fontSize);

// Drop-in replacements for DrawText/MeasureText
// These automatically use custom font if loaded, otherwise fall back to default
void DrawTextCustom(const char *text, int posX, int posY, int fontSize, Color color);
int MeasureTextCustom(const char *text, int fontSize);

#endif // FONT_H

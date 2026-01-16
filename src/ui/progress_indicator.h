#ifndef PROGRESS_INDICATOR_H
#define PROGRESS_INDICATOR_H

#include "raylib.h"
#include <stdbool.h>

// Progress indicator types
typedef enum ProgressType {
    PROGRESS_SPINNER,      // Rotating spinner (8 dots)
    PROGRESS_DOTS,         // Animated dots (...)
    PROGRESS_BAR           // Linear progress bar
} ProgressType;

// Progress indicator state
typedef struct ProgressIndicator {
    ProgressType type;
    float animation_time;  // Accumulated time for animation
    int progress;          // 0-100 for progress bar
    bool visible;
    char message[128];     // Optional status message
} ProgressIndicator;

// Initialize progress indicator
void progress_indicator_init(ProgressIndicator *pi, ProgressType type);

// Update animation (call each frame with delta_time)
void progress_indicator_update(ProgressIndicator *pi, float delta_time);

// Set message to display
void progress_indicator_set_message(ProgressIndicator *pi, const char *msg);

// Set progress value (0-100, for PROGRESS_BAR)
void progress_indicator_set_progress(ProgressIndicator *pi, int progress);

// Draw spinner at position (centered)
// cx, cy: center position, radius: ring radius, color: spinner color
void progress_indicator_draw_spinner(ProgressIndicator *pi, int cx, int cy, int radius, Color color);

// Draw animated dots after text
// x, y: position, font_size: text size, color: dot color
void progress_indicator_draw_dots(ProgressIndicator *pi, int x, int y, int font_size, Color color);

// Draw progress bar
// x, y, width, height: dimensions, bg: background color, fill: fill color
void progress_indicator_draw_bar(ProgressIndicator *pi, int x, int y, int width, int height, Color bg, Color fill);

// Draw full indicator with message (centered in rect)
// bounds: bounding rectangle, color: spinner/indicator color
void progress_indicator_draw(ProgressIndicator *pi, Rectangle bounds, Color color);

// Draw overlay with spinner and message (fullscreen or area overlay)
// bounds: area to cover with semi-transparent overlay, color: spinner color
void progress_indicator_draw_overlay(ProgressIndicator *pi, Rectangle bounds, Color color);

#endif // PROGRESS_INDICATOR_H

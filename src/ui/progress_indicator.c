#include "progress_indicator.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

// Spinner constants
#define SPINNER_DOTS 8
#define SPINNER_DOT_RADIUS 3
#define SPINNER_ROTATION_SPEED 1.0f  // Full rotation per second

// Dots animation constants
#define DOTS_CYCLE_SPEED 0.5f  // Cycle through 0-3 dots every 0.5s

// Animation time wrap value (prevent float overflow)
#define ANIMATION_TIME_WRAP 1000.0f

void progress_indicator_init(ProgressIndicator *pi, ProgressType type)
{
    if (!pi) return;

    pi->type = type;
    pi->animation_time = 0.0f;
    pi->progress = 0;
    pi->visible = false;
    pi->message[0] = '\0';
}

void progress_indicator_update(ProgressIndicator *pi, float delta_time)
{
    if (!pi) return;

    // Wrap to prevent float overflow after long runtime
    pi->animation_time = fmodf(pi->animation_time + delta_time, ANIMATION_TIME_WRAP);
}

void progress_indicator_set_message(ProgressIndicator *pi, const char *msg)
{
    if (!pi) return;

    if (msg) {
        strncpy(pi->message, msg, sizeof(pi->message) - 1);
        pi->message[sizeof(pi->message) - 1] = '\0';
    } else {
        pi->message[0] = '\0';
    }
}

void progress_indicator_set_progress(ProgressIndicator *pi, int progress)
{
    if (!pi) return;

    // Clamp to 0-100
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    pi->progress = progress;
}

void progress_indicator_draw_spinner(ProgressIndicator *pi, int cx, int cy, int radius, Color color)
{
    if (!pi) return;

    // Calculate rotation angle based on time (360 degrees per second)
    float angle = fmodf(pi->animation_time * 360.0f * SPINNER_ROTATION_SPEED, 360.0f);

    // Draw 8 dots in a circle with fading alpha
    for (int i = 0; i < SPINNER_DOTS; i++) {
        // Calculate position for this dot
        float dot_angle = angle + (float)i * (360.0f / SPINNER_DOTS);
        float rad = dot_angle * DEG2RAD;
        int x = cx + (int)(cosf(rad) * radius);
        int y = cy + (int)(sinf(rad) * radius);

        // Calculate alpha (trailing effect - first dot brightest)
        unsigned char alpha = (unsigned char)(255 - i * (230 / SPINNER_DOTS));

        Color dot_color = color;
        dot_color.a = alpha;

        DrawCircle(x, y, SPINNER_DOT_RADIUS, dot_color);
    }
}

void progress_indicator_draw_dots(ProgressIndicator *pi, int x, int y, int font_size, Color color)
{
    if (!pi) return;

    // Calculate number of dots (0-3) based on time
    float cycle = fmodf(pi->animation_time / DOTS_CYCLE_SPEED, 4.0f);
    int dot_count = (int)cycle;
    if (dot_count > 3) dot_count = 3;

    // Build dots string
    char dots[4] = "";
    for (int i = 0; i < dot_count; i++) {
        dots[i] = '.';
    }
    dots[dot_count] = '\0';

    // Draw dots
    Font font = font_get(font_size);
    DrawTextEx(font, dots, (Vector2){(float)x, (float)y}, (float)font_size, 1, color);
}

void progress_indicator_draw_bar(ProgressIndicator *pi, int x, int y, int width, int height, Color bg, Color fill)
{
    if (!pi) return;

    // Draw background
    DrawRectangle(x, y, width, height, bg);

    // Draw fill based on progress
    int fill_width = (width * pi->progress) / 100;
    if (fill_width > 0) {
        DrawRectangle(x, y, fill_width, height, fill);
    }

    // Draw border
    DrawRectangleLinesEx((Rectangle){(float)x, (float)y, (float)width, (float)height}, 1, g_theme.border);
}

void progress_indicator_draw(ProgressIndicator *pi, Rectangle bounds, Color color)
{
    if (!pi) return;

    int cx = (int)(bounds.x + bounds.width / 2);
    int cy = (int)(bounds.y + bounds.height / 2);

    // Draw based on type
    switch (pi->type) {
        case PROGRESS_SPINNER:
            // Draw spinner centered
            progress_indicator_draw_spinner(pi, cx, cy - 15, 15, color);

            // Draw message below spinner if set
            if (pi->message[0] != '\0') {
                Font font = font_get(14);
                Vector2 msg_size = MeasureTextEx(font, pi->message, 14, 1);
                int msg_x = cx - (int)(msg_size.x / 2);
                int msg_y = cy + 15;
                DrawTextEx(font, pi->message, (Vector2){(float)msg_x, (float)msg_y}, 14, 1, g_theme.textSecondary);
            }
            break;

        case PROGRESS_DOTS:
            // Draw message with animated dots
            if (pi->message[0] != '\0') {
                Font font = font_get(14);
                Vector2 msg_size = MeasureTextEx(font, pi->message, 14, 1);
                int msg_x = cx - (int)(msg_size.x / 2) - 10;  // Offset for dots
                int msg_y = cy - 7;
                DrawTextEx(font, pi->message, (Vector2){(float)msg_x, (float)msg_y}, 14, 1, color);
                progress_indicator_draw_dots(pi, msg_x + (int)msg_size.x, msg_y, 14, color);
            }
            break;

        case PROGRESS_BAR:
            // Draw progress bar centered
            {
                int bar_width = (int)(bounds.width * 0.8f);
                int bar_height = 12;
                int bar_x = cx - bar_width / 2;
                int bar_y = cy - bar_height / 2;
                progress_indicator_draw_bar(pi, bar_x, bar_y, bar_width, bar_height, g_theme.hover, color);

                // Draw percentage text above bar
                char pct[8];
                snprintf(pct, sizeof(pct), "%d%%", pi->progress);
                Font font = font_get(12);
                Vector2 pct_size = MeasureTextEx(font, pct, 12, 1);
                DrawTextEx(font, pct, (Vector2){(float)(cx - (int)(pct_size.x / 2)), (float)(bar_y - 18)}, 12, 1, g_theme.textSecondary);

                // Draw message below bar if set
                if (pi->message[0] != '\0') {
                    Vector2 msg_size = MeasureTextEx(font, pi->message, 12, 1);
                    DrawTextEx(font, pi->message, (Vector2){(float)(cx - (int)(msg_size.x / 2)), (float)(bar_y + bar_height + 5)}, 12, 1, g_theme.textSecondary);
                }
            }
            break;
    }
}

void progress_indicator_draw_overlay(ProgressIndicator *pi, Rectangle bounds, Color color)
{
    if (!pi) return;

    // Draw semi-transparent overlay
    DrawRectangle((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height, (Color){0, 0, 0, 180});

    // Draw rounded rectangle container in center
    int container_width = 300;
    int container_height = 120;
    int container_x = (int)(bounds.x + bounds.width / 2 - container_width / 2);
    int container_y = (int)(bounds.y + bounds.height / 2 - container_height / 2);

    DrawRectangle(container_x, container_y, container_width, container_height, g_theme.sidebar);
    DrawRectangleLinesEx((Rectangle){(float)container_x, (float)container_y, (float)container_width, (float)container_height}, 2, g_theme.border);

    // Draw progress indicator inside container
    Rectangle container_bounds = {(float)container_x, (float)container_y, (float)container_width, (float)container_height};
    progress_indicator_draw(pi, container_bounds, color);
}

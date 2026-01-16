#include "dialog.h"
#include "../app.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "raylib.h"

#include <string.h>
#include <stdlib.h>

// Dialog dimensions
#define DIALOG_WIDTH 400
#define DIALOG_HEIGHT 160
#define DIALOG_PADDING 20
#define BUTTON_WIDTH 80
#define BUTTON_HEIGHT 28
#define BUTTON_SPACING 12
#define DIALOG_TITLE_HEIGHT 32

// Summary dialog dimensions (larger)
#define SUMMARY_DIALOG_WIDTH 500
#define SUMMARY_DIALOG_HEIGHT 400
#define SUMMARY_TEXT_HEIGHT 300
#define SUMMARY_LINE_HEIGHT 18
#define SUMMARY_VISIBLE_LINES 16

// Safe string copy helper
static void safe_strncpy(char *dest, const char *src, size_t dest_size)
{
    if (dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// Internal helper to show dialog
static void dialog_show(DialogState *dialog, DialogType type, const char *title,
                        const char *message, DialogButton default_btn,
                        DialogCallback on_confirm)
{
    dialog->visible = true;
    dialog->type = type;
    dialog->selected = default_btn;
    dialog->on_confirm = on_confirm;

    safe_strncpy(dialog->title, title, sizeof(dialog->title));
    safe_strncpy(dialog->message, message, sizeof(dialog->message));
}

// Draw a button with centered text
static void draw_button(int x, int y, int width, int height,
                        const char *text, Color bg, Color text_color)
{
    DrawRectangle(x, y, width, height, bg);
    DrawRectangleLinesEx((Rectangle){(float)x, (float)y, (float)width, (float)height},
                         1, g_theme.border);
    int text_width = MeasureTextCustom(text, FONT_SIZE);
    DrawTextCustom(text, x + (width - text_width) / 2,
             y + (height - FONT_SIZE) / 2, FONT_SIZE, text_color);
}

void dialog_init(DialogState *dialog)
{
    memset(dialog, 0, sizeof(DialogState));
    dialog->visible = false;
    dialog->type = DIALOG_NONE;
    dialog->selected = DIALOG_BTN_CANCEL;
    dialog->on_confirm = NULL;
    dialog->user_data = NULL;
}

void dialog_confirm(DialogState *dialog, const char *title, const char *message,
                    DialogCallback on_confirm)
{
    dialog_show(dialog, DIALOG_CONFIRM, title, message, DIALOG_BTN_CANCEL, on_confirm);
}

void dialog_error(DialogState *dialog, const char *title, const char *message)
{
    dialog_show(dialog, DIALOG_ERROR, title, message, DIALOG_BTN_OK, NULL);
}

void dialog_info(DialogState *dialog, const char *title, const char *message)
{
    dialog_show(dialog, DIALOG_INFO, title, message, DIALOG_BTN_OK, NULL);
}

// Helper to count wrapped lines for a given text and width
static int count_wrapped_lines(const char *text, int max_chars_per_line)
{
    if (!text || !*text) return 0;

    int lines = 1;
    int col = 0;

    while (*text) {
        if (*text == '\n') {
            lines++;
            col = 0;
        } else {
            col++;
            if (col >= max_chars_per_line) {
                lines++;
                col = 0;
            }
        }
        text++;
    }
    return lines;
}

void dialog_summary(DialogState *dialog, const char *title, const char *summary)
{
    // Free any existing summary content
    if (dialog->summary_content) {
        free(dialog->summary_content);
        dialog->summary_content = NULL;
    }

    dialog->visible = true;
    dialog->type = DIALOG_SUMMARY;
    dialog->selected = DIALOG_BTN_OK;
    dialog->on_confirm = NULL;
    dialog->summary_scroll = 0;

    safe_strncpy(dialog->title, title, sizeof(dialog->title));

    // Allocate and copy summary content
    size_t len = strlen(summary);
    if (len > DIALOG_SUMMARY_MAX - 1) {
        len = DIALOG_SUMMARY_MAX - 1;
    }
    dialog->summary_content = (char *)malloc(len + 1);
    if (dialog->summary_content) {
        memcpy(dialog->summary_content, summary, len);
        dialog->summary_content[len] = '\0';

        // Calculate total lines (approximate chars per line based on dialog width)
        int chars_per_line = (SUMMARY_DIALOG_WIDTH - DIALOG_PADDING * 2) / 8;
        dialog->summary_total_lines = count_wrapped_lines(dialog->summary_content, chars_per_line);
    }
}

void dialog_hide(DialogState *dialog)
{
    dialog->visible = false;
    dialog->type = DIALOG_NONE;
    dialog->on_confirm = NULL;
    dialog->user_data = NULL;

    // Free summary content if any
    if (dialog->summary_content) {
        free(dialog->summary_content);
        dialog->summary_content = NULL;
    }
    dialog->summary_scroll = 0;
    dialog->summary_total_lines = 0;
}

bool dialog_is_visible(DialogState *dialog)
{
    return dialog->visible;
}

bool dialog_handle_input(struct App *app)
{
    DialogState *dialog = &app->dialog;

    if (!dialog->visible) {
        return false;
    }

    // Escape cancels the dialog
    if (IsKeyPressed(KEY_ESCAPE)) {
        dialog_hide(dialog);
        return true;
    }

    // Enter confirms the selected button
    if (IsKeyPressed(KEY_ENTER)) {
        if (dialog->type == DIALOG_CONFIRM) {
            if (dialog->selected == DIALOG_BTN_OK && dialog->on_confirm) {
                dialog->on_confirm(app);
            }
        }
        dialog_hide(dialog);
        return true;
    }

    // Tab or arrow keys switch between buttons (for confirm dialogs)
    if (dialog->type == DIALOG_CONFIRM) {
        if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) ||
            IsKeyPressed(KEY_H) || IsKeyPressed(KEY_L)) {
            dialog->selected = (dialog->selected == DIALOG_BTN_OK) ?
                               DIALOG_BTN_CANCEL : DIALOG_BTN_OK;
            return true;
        }
    }

    // Summary dialog scrolling
    if (dialog->type == DIALOG_SUMMARY) {
        int max_scroll = dialog->summary_total_lines - SUMMARY_VISIBLE_LINES;
        if (max_scroll < 0) max_scroll = 0;

        // Keyboard scrolling
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_K)) {
            dialog->summary_scroll--;
            if (dialog->summary_scroll < 0) dialog->summary_scroll = 0;
            return true;
        }
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_J)) {
            dialog->summary_scroll++;
            if (dialog->summary_scroll > max_scroll) dialog->summary_scroll = max_scroll;
            return true;
        }
        if (IsKeyPressed(KEY_PAGE_UP)) {
            dialog->summary_scroll -= SUMMARY_VISIBLE_LINES;
            if (dialog->summary_scroll < 0) dialog->summary_scroll = 0;
            return true;
        }
        if (IsKeyPressed(KEY_PAGE_DOWN)) {
            dialog->summary_scroll += SUMMARY_VISIBLE_LINES;
            if (dialog->summary_scroll > max_scroll) dialog->summary_scroll = max_scroll;
            return true;
        }

        // Mouse wheel scrolling
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            dialog->summary_scroll -= (int)(wheel * 3);
            if (dialog->summary_scroll < 0) dialog->summary_scroll = 0;
            if (dialog->summary_scroll > max_scroll) dialog->summary_scroll = max_scroll;
            return true;
        }
    }

    // Mouse click on buttons
    Vector2 mouse = GetMousePosition();
    int dialog_x = (app->width - DIALOG_WIDTH) / 2;
    int dialog_y = (app->height - DIALOG_HEIGHT) / 2;

    if (dialog->type == DIALOG_CONFIRM) {
        // Two buttons: Cancel and OK
        int buttons_width = BUTTON_WIDTH * 2 + BUTTON_SPACING;
        int buttons_x = dialog_x + (DIALOG_WIDTH - buttons_width) / 2;
        int buttons_y = dialog_y + DIALOG_HEIGHT - DIALOG_PADDING - BUTTON_HEIGHT;

        Rectangle cancel_rect = {
            (float)buttons_x,
            (float)buttons_y,
            (float)BUTTON_WIDTH,
            (float)BUTTON_HEIGHT
        };

        Rectangle ok_rect = {
            (float)(buttons_x + BUTTON_WIDTH + BUTTON_SPACING),
            (float)buttons_y,
            (float)BUTTON_WIDTH,
            (float)BUTTON_HEIGHT
        };

        if (CheckCollisionPointRec(mouse, cancel_rect)) {
            dialog->selected = DIALOG_BTN_CANCEL;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                dialog_hide(dialog);
                return true;
            }
        }

        if (CheckCollisionPointRec(mouse, ok_rect)) {
            dialog->selected = DIALOG_BTN_OK;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (dialog->on_confirm) {
                    dialog->on_confirm(app);
                }
                dialog_hide(dialog);
                return true;
            }
        }
    } else if (dialog->type == DIALOG_SUMMARY) {
        // Summary dialog - use larger dimensions
        int summary_dialog_x = (app->width - SUMMARY_DIALOG_WIDTH) / 2;
        int summary_dialog_y = (app->height - SUMMARY_DIALOG_HEIGHT) / 2;
        int button_x = summary_dialog_x + (SUMMARY_DIALOG_WIDTH - BUTTON_WIDTH) / 2;
        int button_y = summary_dialog_y + SUMMARY_DIALOG_HEIGHT - DIALOG_PADDING - BUTTON_HEIGHT;

        Rectangle ok_rect = {
            (float)button_x,
            (float)button_y,
            (float)BUTTON_WIDTH,
            (float)BUTTON_HEIGHT
        };

        if (CheckCollisionPointRec(mouse, ok_rect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            dialog_hide(dialog);
            return true;
        }
    } else {
        // Single OK button for info/error dialogs
        int button_x = dialog_x + (DIALOG_WIDTH - BUTTON_WIDTH) / 2;
        int button_y = dialog_y + DIALOG_HEIGHT - DIALOG_PADDING - BUTTON_HEIGHT;

        Rectangle ok_rect = {
            (float)button_x,
            (float)button_y,
            (float)BUTTON_WIDTH,
            (float)BUTTON_HEIGHT
        };

        if (CheckCollisionPointRec(mouse, ok_rect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            dialog_hide(dialog);
            return true;
        }
    }

    return true; // Dialog is visible, consume input
}

// Helper to draw summary dialog
static void draw_summary_dialog(struct App *app)
{
    DialogState *dialog = &app->dialog;

    int dialog_x = (app->width - SUMMARY_DIALOG_WIDTH) / 2;
    int dialog_y = (app->height - SUMMARY_DIALOG_HEIGHT) / 2;

    // Dialog background with shadow
    DrawRectangle(dialog_x + 4, dialog_y + 4, SUMMARY_DIALOG_WIDTH, SUMMARY_DIALOG_HEIGHT, Fade(BLACK, 0.3f));
    DrawRectangle(dialog_x, dialog_y, SUMMARY_DIALOG_WIDTH, SUMMARY_DIALOG_HEIGHT, g_theme.sidebar);
    DrawRectangleLinesEx((Rectangle){(float)dialog_x, (float)dialog_y, SUMMARY_DIALOG_WIDTH, SUMMARY_DIALOG_HEIGHT},
                         1, g_theme.border);

    // Title bar (AI accent color for summary)
    DrawRectangle(dialog_x, dialog_y, SUMMARY_DIALOG_WIDTH, DIALOG_TITLE_HEIGHT, Fade(g_theme.aiAccent, 0.3f));
    DrawLine(dialog_x, dialog_y + DIALOG_TITLE_HEIGHT, dialog_x + SUMMARY_DIALOG_WIDTH, dialog_y + DIALOG_TITLE_HEIGHT, g_theme.border);

    // Title text
    DrawTextCustom(dialog->title, dialog_x + DIALOG_PADDING, dialog_y + 8, FONT_SIZE, g_theme.textPrimary);

    // Summary content area (scrollable)
    int content_x = dialog_x + DIALOG_PADDING;
    int content_y = dialog_y + DIALOG_TITLE_HEIGHT + 8;
    int content_width = SUMMARY_DIALOG_WIDTH - DIALOG_PADDING * 2;
    int content_height = SUMMARY_TEXT_HEIGHT;

    // Draw content background
    DrawRectangle(content_x - 4, content_y - 4, content_width + 8, content_height + 8, Fade(g_theme.background, 0.5f));
    DrawRectangleLinesEx((Rectangle){(float)(content_x - 4), (float)(content_y - 4),
                                      (float)(content_width + 8), (float)(content_height + 8)},
                         1, g_theme.border);

    // Enable scissor mode to clip text to content area
    BeginScissorMode(content_x, content_y, content_width, content_height);

    // Draw summary text with word wrap
    if (dialog->summary_content) {
        int char_width = 8;
        int max_chars = content_width / char_width;
        char *text = dialog->summary_content;
        int line_num = 0;
        int draw_y = content_y;

        while (*text) {
            char line[128];
            int line_len = 0;

            // Build a line
            while (*text && *text != '\n' && line_len < max_chars - 1 && line_len < 127) {
                line[line_len++] = *text++;
            }
            line[line_len] = '\0';

            // Skip to end of word if we broke mid-word
            if (*text && *text != '\n' && *text != ' ') {
                // Backtrack to last space
                int i = line_len - 1;
                while (i > 0 && line[i] != ' ') {
                    i--;
                    text--;
                }
                if (i > 0) {
                    line[i] = '\0';
                }
            }

            // Skip newline or space
            if (*text == '\n' || *text == ' ') text++;

            // Only draw visible lines
            if (line_num >= dialog->summary_scroll &&
                line_num < dialog->summary_scroll + SUMMARY_VISIBLE_LINES) {
                DrawTextCustom(line, content_x, draw_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                draw_y += SUMMARY_LINE_HEIGHT;
            }
            line_num++;
        }
    }

    EndScissorMode();

    // Draw scrollbar if needed
    if (dialog->summary_total_lines > SUMMARY_VISIBLE_LINES) {
        int scrollbar_x = content_x + content_width + 2;
        int scrollbar_height = content_height;
        float visible_ratio = (float)SUMMARY_VISIBLE_LINES / dialog->summary_total_lines;
        int thumb_height = (int)(scrollbar_height * visible_ratio);
        if (thumb_height < 20) thumb_height = 20;

        float scroll_ratio = 0;
        int max_scroll = dialog->summary_total_lines - SUMMARY_VISIBLE_LINES;
        if (max_scroll > 0) {
            scroll_ratio = (float)dialog->summary_scroll / max_scroll;
        }
        int thumb_y = content_y + (int)((scrollbar_height - thumb_height) * scroll_ratio);

        DrawRectangle(scrollbar_x, content_y, 6, scrollbar_height, Fade(g_theme.border, 0.3f));
        DrawRectangle(scrollbar_x, thumb_y, 6, thumb_height, g_theme.textSecondary);
    }

    // OK button
    int button_x = dialog_x + (SUMMARY_DIALOG_WIDTH - BUTTON_WIDTH) / 2;
    int button_y = dialog_y + SUMMARY_DIALOG_HEIGHT - DIALOG_PADDING - BUTTON_HEIGHT;
    draw_button(button_x, button_y, BUTTON_WIDTH, BUTTON_HEIGHT, "OK", g_theme.selection, g_theme.textPrimary);

    // Keyboard hint
    const char *hint = "Up/Down to scroll, Enter or Escape to close";
    int hint_width = MeasureTextCustom(hint, FONT_SIZE_SMALL);
    DrawTextCustom(hint, dialog_x + (SUMMARY_DIALOG_WIDTH - hint_width) / 2,
             dialog_y + SUMMARY_DIALOG_HEIGHT + 8, FONT_SIZE_SMALL, g_theme.textSecondary);
}

void dialog_draw(struct App *app)
{
    DialogState *dialog = &app->dialog;

    if (!dialog->visible) {
        return;
    }

    // Draw semi-transparent overlay
    DrawRectangle(0, 0, app->width, app->height, Fade(BLACK, 0.5f));

    // Handle summary dialog separately (different size)
    if (dialog->type == DIALOG_SUMMARY) {
        draw_summary_dialog(app);
        return;
    }

    // Dialog box position (centered)
    int dialog_x = (app->width - DIALOG_WIDTH) / 2;
    int dialog_y = (app->height - DIALOG_HEIGHT) / 2;

    // Dialog background with shadow
    DrawRectangle(dialog_x + 4, dialog_y + 4, DIALOG_WIDTH, DIALOG_HEIGHT, Fade(BLACK, 0.3f));
    DrawRectangle(dialog_x, dialog_y, DIALOG_WIDTH, DIALOG_HEIGHT, g_theme.sidebar);
    DrawRectangleLinesEx((Rectangle){(float)dialog_x, (float)dialog_y, DIALOG_WIDTH, DIALOG_HEIGHT},
                         1, g_theme.border);

    // Title bar
    Color title_bg = g_theme.error;
    if (dialog->type == DIALOG_INFO) {
        title_bg = g_theme.accent;
    } else if (dialog->type == DIALOG_CONFIRM) {
        title_bg = g_theme.warning;
    }

    DrawRectangle(dialog_x, dialog_y, DIALOG_WIDTH, 32, Fade(title_bg, 0.3f));
    DrawLine(dialog_x, dialog_y + 32, dialog_x + DIALOG_WIDTH, dialog_y + 32, g_theme.border);

    // Title text
    DrawTextCustom(dialog->title, dialog_x + DIALOG_PADDING, dialog_y + 8, FONT_SIZE, g_theme.textPrimary);

    // Message text (wrap if needed)
    int message_y = dialog_y + 48;
    int max_width = DIALOG_WIDTH - DIALOG_PADDING * 2;
    int char_width = 8; // Approximate character width for FONT_SIZE
    int max_chars = max_width / char_width;

    char *msg = dialog->message;
    int line_count = 0;
    while (*msg && line_count < 3) {
        char line[128];
        int len = (int)strlen(msg);
        if (len > max_chars) {
            // Find last space before max_chars
            int break_at = max_chars;
            for (int i = max_chars; i > 0; i--) {
                if (msg[i] == ' ') {
                    break_at = i;
                    break;
                }
            }
            strncpy(line, msg, break_at);
            line[break_at] = '\0';
            msg += break_at;
            if (*msg == ' ') msg++; // Skip space
        } else {
            strncpy(line, msg, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            msg += len;
        }

        DrawTextCustom(line, dialog_x + DIALOG_PADDING, message_y, FONT_SIZE, g_theme.textSecondary);
        message_y += ROW_HEIGHT;
        line_count++;
    }

    // Buttons
    int button_y = dialog_y + DIALOG_HEIGHT - DIALOG_PADDING - BUTTON_HEIGHT;

    if (dialog->type == DIALOG_CONFIRM) {
        // Two buttons: Cancel and Delete/OK
        int buttons_width = BUTTON_WIDTH * 2 + BUTTON_SPACING;
        int buttons_x = dialog_x + (DIALOG_WIDTH - buttons_width) / 2;

        // Cancel button
        Color cancel_bg = (dialog->selected == DIALOG_BTN_CANCEL) ? g_theme.selection : g_theme.hover;
        Color cancel_text = (dialog->selected == DIALOG_BTN_CANCEL) ? g_theme.textPrimary : g_theme.textSecondary;
        draw_button(buttons_x, button_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Cancel", cancel_bg, cancel_text);

        // Delete button
        int ok_x = buttons_x + BUTTON_WIDTH + BUTTON_SPACING;
        Color ok_bg = (dialog->selected == DIALOG_BTN_OK) ? g_theme.error : Fade(g_theme.error, 0.6f);
        draw_button(ok_x, button_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Delete", ok_bg, WHITE);
    } else {
        // Single OK button
        int button_x = dialog_x + (DIALOG_WIDTH - BUTTON_WIDTH) / 2;
        draw_button(button_x, button_y, BUTTON_WIDTH, BUTTON_HEIGHT, "OK", g_theme.selection, g_theme.textPrimary);
    }

    // Keyboard hint
    const char *hint = dialog->type == DIALOG_CONFIRM ?
                       "Tab/Arrow to switch, Enter to confirm, Escape to cancel" :
                       "Enter or Escape to close";
    int hint_width = MeasureTextCustom(hint, FONT_SIZE_SMALL);
    DrawTextCustom(hint, dialog_x + (DIALOG_WIDTH - hint_width) / 2,
             dialog_y + DIALOG_HEIGHT + 8, FONT_SIZE_SMALL, g_theme.textSecondary);
}

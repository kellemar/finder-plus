#include "file_view_modal.h"
#include "../app.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Modal dimensions
#define MODAL_MARGIN 40              // Margin from screen edges
#define MODAL_PADDING 20             // Internal padding
#define MODAL_TITLE_HEIGHT 40        // Title bar height
#define MODAL_LINE_HEIGHT 20         // Line height for text
#define MODAL_SCROLLBAR_WIDTH 8      // Scrollbar width
#define MODAL_LINE_NUMBER_WIDTH 50   // Space for line numbers + margin

// Safe string copy helper
static void safe_strncpy(char *dest, const char *src, size_t dest_size)
{
    if (dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// Count lines in text content
static int count_lines(const char *text)
{
    if (!text || !*text) return 0;

    int lines = 1;
    const char *start = text;
    while (*text) {
        if (*text == '\n') {
            lines++;
        }
        text++;
    }
    // Don't count trailing newline as extra line
    if (text > start && *(text - 1) == '\n') {
        lines--;
    }
    return lines;
}

// Extract filename from full path
static void extract_filename(const char *path, char *filename, size_t filename_size)
{
    if (!path || !*path) {
        filename[0] = '\0';
        return;
    }

    const char *last_slash = strrchr(path, '/');
    if (last_slash) {
        safe_strncpy(filename, last_slash + 1, filename_size);
    } else {
        safe_strncpy(filename, path, filename_size);
    }
}

// Free text content if allocated
static void free_text_content(FileViewModalState *modal)
{
    if (modal->text_content) {
        free(modal->text_content);
        modal->text_content = NULL;
    }
}

// Calculate maximum scroll offset (clamped to 0)
static int calc_max_scroll(FileViewModalState *modal)
{
    int max = modal->total_lines - modal->visible_lines;
    return max > 0 ? max : 0;
}

void file_view_modal_init(FileViewModalState *modal)
{
    // Zero-initialization sets: visible=false, type=FILE_VIEW_NONE,
    // text_content=NULL, all integers=0, all strings=""
    memset(modal, 0, sizeof(FileViewModalState));
}

void file_view_modal_free(FileViewModalState *modal)
{
    free_text_content(modal);
    // Note: We don't unload the texture here because it's owned by preview
    modal->texture_id = 0;
    modal->visible = false;
    modal->type = FILE_VIEW_NONE;
    modal->total_lines = 0;
    modal->scroll_offset = 0;
}

void file_view_modal_show_text(FileViewModalState *modal, const char *file_path,
                               const char *content)
{
    free_text_content(modal);

    modal->visible = true;
    modal->type = FILE_VIEW_TEXT;
    modal->scroll_offset = 0;

    safe_strncpy(modal->file_path, file_path, sizeof(modal->file_path));
    extract_filename(file_path, modal->filename, sizeof(modal->filename));

    // Copy content
    if (content && *content) {
        size_t len = strlen(content);
        modal->text_content = (char *)malloc(len + 1);
        if (modal->text_content) {
            memcpy(modal->text_content, content, len + 1);
            modal->total_lines = count_lines(content);
        } else {
            modal->total_lines = 0;
        }
    } else {
        modal->text_content = NULL;
        modal->total_lines = 0;
    }
}

void file_view_modal_show_image(FileViewModalState *modal, const char *file_path,
                                unsigned int texture_id, int width, int height)
{
    free_text_content(modal);

    modal->visible = true;
    modal->type = FILE_VIEW_IMAGE;
    modal->scroll_offset = 0;
    modal->total_lines = 0;

    safe_strncpy(modal->file_path, file_path, sizeof(modal->file_path));
    extract_filename(file_path, modal->filename, sizeof(modal->filename));

    modal->texture_id = texture_id;
    modal->image_width = width;
    modal->image_height = height;
}

void file_view_modal_hide(FileViewModalState *modal)
{
    free_text_content(modal);

    modal->visible = false;
    modal->type = FILE_VIEW_NONE;
    modal->scroll_offset = 0;
    modal->total_lines = 0;
    modal->texture_id = 0;
}

bool file_view_modal_is_visible(FileViewModalState *modal)
{
    return modal->visible;
}

void file_view_modal_scroll_down(FileViewModalState *modal, int lines)
{
    if (modal->type != FILE_VIEW_TEXT) return;

    modal->scroll_offset += lines;

    int max_scroll = calc_max_scroll(modal);
    if (modal->scroll_offset > max_scroll) {
        modal->scroll_offset = max_scroll;
    }
}

void file_view_modal_scroll_up(FileViewModalState *modal, int lines)
{
    if (modal->type != FILE_VIEW_TEXT) return;

    modal->scroll_offset -= lines;
    if (modal->scroll_offset < 0) {
        modal->scroll_offset = 0;
    }
}

void file_view_modal_scroll_to_top(FileViewModalState *modal)
{
    if (modal->type != FILE_VIEW_TEXT) return;
    modal->scroll_offset = 0;
}

void file_view_modal_scroll_to_bottom(FileViewModalState *modal)
{
    if (modal->type != FILE_VIEW_TEXT) return;
    modal->scroll_offset = calc_max_scroll(modal);
}

const char* file_view_modal_get_filename(FileViewModalState *modal)
{
    return modal->filename;
}

bool file_view_modal_handle_input(struct App *app)
{
    if (!app) return false;

    FileViewModalState *modal = &app->file_view_modal;

    if (!modal->visible) {
        return false;
    }

    // Escape closes the modal
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_Q)) {
        file_view_modal_hide(modal);
        return true;
    }

    // Text scrolling controls
    if (modal->type == FILE_VIEW_TEXT) {
        // j/Down: scroll down
        if (IsKeyPressed(KEY_J) || IsKeyPressed(KEY_DOWN)) {
            file_view_modal_scroll_down(modal, 1);
            return true;
        }

        // k/Up: scroll up
        if (IsKeyPressed(KEY_K) || IsKeyPressed(KEY_UP)) {
            file_view_modal_scroll_up(modal, 1);
            return true;
        }

        // Page Down / Ctrl+D
        if (IsKeyPressed(KEY_PAGE_DOWN) ||
            (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_D))) {
            file_view_modal_scroll_down(modal, modal->visible_lines / 2);
            return true;
        }

        // Page Up / Ctrl+U
        if (IsKeyPressed(KEY_PAGE_UP) ||
            (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_U))) {
            file_view_modal_scroll_up(modal, modal->visible_lines / 2);
            return true;
        }

        // g: scroll to top (gg pattern)
        if (IsKeyPressed(KEY_G)) {
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                // G: scroll to bottom
                file_view_modal_scroll_to_bottom(modal);
            } else {
                // g: first press starts gg timer (simplified: just go to top)
                file_view_modal_scroll_to_top(modal);
            }
            return true;
        }

        // Mouse wheel scrolling
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            if (wheel > 0) {
                file_view_modal_scroll_up(modal, 3);
            } else {
                file_view_modal_scroll_down(modal, 3);
            }
            return true;
        }
    }

    // Click outside modal to close (optional UX)
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        int modal_x = MODAL_MARGIN;
        int modal_y = MODAL_MARGIN;
        int modal_width = app->width - MODAL_MARGIN * 2;
        int modal_height = app->height - MODAL_MARGIN * 2;

        Rectangle modal_rect = {
            (float)modal_x,
            (float)modal_y,
            (float)modal_width,
            (float)modal_height
        };

        if (!CheckCollisionPointRec(mouse, modal_rect)) {
            file_view_modal_hide(modal);
            return true;
        }
    }

    return true; // Modal is visible, consume all input
}

// Draw text view
static void draw_text_view(struct App *app, int content_x, int content_y,
                           int content_width, int content_height)
{
    FileViewModalState *modal = &app->file_view_modal;

    if (!modal->text_content) return;

    // Calculate visible lines
    modal->visible_lines = content_height / MODAL_LINE_HEIGHT;

    // Enable scissor mode for content clipping
    BeginScissorMode(content_x, content_y, content_width, content_height);

    const char *text = modal->text_content;
    int line_num = 0;
    int draw_y = content_y;

    while (*text && draw_y < content_y + content_height) {
        // Find end of line
        const char *line_end = strchr(text, '\n');
        int line_len = line_end ? (int)(line_end - text) : (int)strlen(text);

        // Only draw visible lines
        if (line_num >= modal->scroll_offset) {
            // Create line buffer (truncate very long lines)
            char line[1024];
            int copy_len = line_len < (int)sizeof(line) - 1 ? line_len : (int)sizeof(line) - 1;
            memcpy(line, text, copy_len);
            line[copy_len] = '\0';

            // Draw line number
            char line_num_str[16];
            snprintf(line_num_str, sizeof(line_num_str), "%4d", line_num + 1);
            DrawTextCustom(line_num_str, content_x, draw_y, FONT_SIZE_SMALL, g_theme.textSecondary);

            // Draw line content
            DrawTextCustom(line, content_x + MODAL_LINE_NUMBER_WIDTH, draw_y, FONT_SIZE_SMALL, g_theme.textPrimary);

            draw_y += MODAL_LINE_HEIGHT;
        }

        line_num++;
        text = line_end ? line_end + 1 : text + line_len;
    }

    EndScissorMode();

    // Draw scrollbar if needed
    if (modal->total_lines > modal->visible_lines) {
        int scrollbar_x = content_x + content_width + 4;
        int scrollbar_height = content_height;

        float visible_ratio = (float)modal->visible_lines / modal->total_lines;
        int thumb_height = (int)(scrollbar_height * visible_ratio);
        if (thumb_height < 20) thumb_height = 20;

        int max_scroll = calc_max_scroll(modal);
        float scroll_ratio = max_scroll > 0 ? (float)modal->scroll_offset / max_scroll : 0;
        int thumb_y = content_y + (int)((scrollbar_height - thumb_height) * scroll_ratio);

        DrawRectangle(scrollbar_x, content_y, MODAL_SCROLLBAR_WIDTH, scrollbar_height,
                      Fade(g_theme.border, 0.3f));
        DrawRectangle(scrollbar_x, thumb_y, MODAL_SCROLLBAR_WIDTH, thumb_height,
                      g_theme.textSecondary);
    }
}

// Draw image view
static void draw_image_view(struct App *app, int content_x, int content_y,
                            int content_width, int content_height)
{
    FileViewModalState *modal = &app->file_view_modal;

    // Validate image before drawing
    if (modal->texture_id == 0 || modal->image_width <= 0 || modal->image_height <= 0) return;

    // Calculate scaled dimensions to fit in content area while maintaining aspect ratio
    float scale_x = (float)content_width / modal->image_width;
    float scale_y = (float)content_height / modal->image_height;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    int draw_width = (int)(modal->image_width * scale);
    int draw_height = (int)(modal->image_height * scale);

    // Center the image
    int draw_x = content_x + (content_width - draw_width) / 2;
    int draw_y = content_y + (content_height - draw_height) / 2;

    // Draw the texture
    Texture2D tex = { modal->texture_id, modal->image_width, modal->image_height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };

    Rectangle source = { 0, 0, (float)modal->image_width, (float)modal->image_height };
    Rectangle dest = { (float)draw_x, (float)draw_y, (float)draw_width, (float)draw_height };

    DrawTexturePro(tex, source, dest, (Vector2){0, 0}, 0.0f, WHITE);

    // Draw image info below
    char info[128];
    snprintf(info, sizeof(info), "%dx%d pixels (%.1f%% zoom)", modal->image_width, modal->image_height, scale * 100);
    int info_width = MeasureTextCustom(info, FONT_SIZE_SMALL);
    DrawTextCustom(info, content_x + (content_width - info_width) / 2,
                   content_y + content_height + 8, FONT_SIZE_SMALL, g_theme.textSecondary);
}

void file_view_modal_draw(struct App *app)
{
    if (!app) return;

    FileViewModalState *modal = &app->file_view_modal;

    if (!modal->visible) return;

    // Draw semi-transparent overlay
    DrawRectangle(0, 0, app->width, app->height, Fade(BLACK, 0.7f));

    // Modal dimensions
    int modal_x = MODAL_MARGIN;
    int modal_y = MODAL_MARGIN;
    int modal_width = app->width - MODAL_MARGIN * 2;
    int modal_height = app->height - MODAL_MARGIN * 2;

    // Modal background with shadow
    DrawRectangle(modal_x + 6, modal_y + 6, modal_width, modal_height, Fade(BLACK, 0.4f));
    DrawRectangle(modal_x, modal_y, modal_width, modal_height, g_theme.sidebar);
    DrawRectangleLinesEx((Rectangle){(float)modal_x, (float)modal_y,
                                      (float)modal_width, (float)modal_height},
                         1, g_theme.border);

    // Title bar
    Color title_bg = modal->type == FILE_VIEW_TEXT ? g_theme.accent : g_theme.aiAccent;
    DrawRectangle(modal_x, modal_y, modal_width, MODAL_TITLE_HEIGHT, Fade(title_bg, 0.3f));
    DrawLine(modal_x, modal_y + MODAL_TITLE_HEIGHT,
             modal_x + modal_width, modal_y + MODAL_TITLE_HEIGHT, g_theme.border);

    // Title text (filename)
    DrawTextCustom(modal->filename, modal_x + MODAL_PADDING, modal_y + 10, FONT_SIZE, g_theme.textPrimary);

    // Close hint
    const char *hint = "Press Escape or Q to close";
    int hint_width = MeasureTextCustom(hint, FONT_SIZE_SMALL);
    DrawTextCustom(hint, modal_x + modal_width - hint_width - MODAL_PADDING,
                   modal_y + 12, FONT_SIZE_SMALL, g_theme.textSecondary);

    // Content area
    int content_x = modal_x + MODAL_PADDING;
    int content_y = modal_y + MODAL_TITLE_HEIGHT + MODAL_PADDING;
    int content_width = modal_width - MODAL_PADDING * 2;
    int content_height = modal_height - MODAL_TITLE_HEIGHT - MODAL_PADDING * 2;

    // Draw content based on type
    if (modal->type == FILE_VIEW_TEXT) {
        // Text needs space for scrollbar
        int text_content_width = content_width - MODAL_SCROLLBAR_WIDTH - 8;
        draw_text_view(app, content_x, content_y, text_content_width, content_height);
    } else if (modal->type == FILE_VIEW_IMAGE) {
        // Images use full width
        draw_image_view(app, content_x, content_y, content_width, content_height);
    }
}

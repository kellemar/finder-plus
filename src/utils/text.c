#include "text.h"
#include "font.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

int draw_text_wrapped(const char *text, int x, int y, int max_width,
                      int max_lines, int font_size, Color color)
{
    if (!text || max_width <= 0 || max_lines <= 0) {
        return 0;
    }

    int line_height = font_size + 2;
    int lines_drawn = 0;
    char line_buffer[256];

    while (*text && lines_drawn < max_lines) {
        int len = 0;
        int last_space = 0;

        // Find how many chars fit in the width
        while (text[len] && text[len] != '\n') {
            if (text[len] == ' ') last_space = len;
            if (len >= (int)sizeof(line_buffer) - 1) break;

            strncpy(line_buffer, text, len + 1);
            line_buffer[len + 1] = '\0';
            if (MeasureTextCustom(line_buffer, font_size) > max_width) break;
            len++;
        }

        // Word wrap at last space if we exceeded width
        if (text[len] && text[len] != '\n' && last_space > 0 && len > 0) {
            len = last_space;
        }

        // Handle empty line (newline character)
        if (len == 0 && text[0] == '\n') {
            len = 1;
            line_buffer[0] = '\0';
        } else {
            strncpy(line_buffer, text, len);
            line_buffer[len] = '\0';
        }

        DrawTextCustom(line_buffer, x, y, font_size, color);

        y += line_height;
        lines_drawn++;
        text += len;

        // Skip whitespace at line start
        while (*text == ' ' || *text == '\n') text++;
    }

    return lines_drawn;
}

int measure_text_lines(const char *text, int max_width, int font_size)
{
    if (!text || max_width <= 0) {
        return 0;
    }

    int lines = 0;
    char line_buffer[256];

    while (*text) {
        int len = 0;
        int last_space = 0;

        while (text[len] && text[len] != '\n') {
            if (text[len] == ' ') last_space = len;
            if (len >= (int)sizeof(line_buffer) - 1) break;

            strncpy(line_buffer, text, len + 1);
            line_buffer[len + 1] = '\0';
            if (MeasureTextCustom(line_buffer, font_size) > max_width) break;
            len++;
        }

        if (text[len] && text[len] != '\n' && last_space > 0 && len > 0) {
            len = last_space;
        }

        if (len == 0 && text[0] == '\n') {
            len = 1;
        }

        lines++;
        text += len;
        while (*text == ' ' || *text == '\n') text++;
    }

    return lines;
}

int draw_text_wrapped_scrolled(const char *text, int x, int y, int max_width,
                               int max_lines, int skip_lines, int font_size, Color color)
{
    if (!text || max_width <= 0 || max_lines <= 0) {
        return 0;
    }

    int line_height = font_size + 2;
    int lines_processed = 0;
    int lines_drawn = 0;
    char line_buffer[256];

    while (*text && lines_drawn < max_lines) {
        int len = 0;
        int last_space = 0;

        // Find how many chars fit in the width
        while (text[len] && text[len] != '\n') {
            if (text[len] == ' ') last_space = len;
            if (len >= (int)sizeof(line_buffer) - 1) break;

            strncpy(line_buffer, text, len + 1);
            line_buffer[len + 1] = '\0';
            if (MeasureTextCustom(line_buffer, font_size) > max_width) break;
            len++;
        }

        // Word wrap at last space if we exceeded width
        if (text[len] && text[len] != '\n' && last_space > 0 && len > 0) {
            len = last_space;
        }

        // Handle empty line (newline character)
        if (len == 0 && text[0] == '\n') {
            len = 1;
            line_buffer[0] = '\0';
        } else {
            strncpy(line_buffer, text, len);
            line_buffer[len] = '\0';
        }

        // Only draw if we've passed the skip count
        if (lines_processed >= skip_lines) {
            DrawTextCustom(line_buffer, x, y, font_size, color);
            y += line_height;
            lines_drawn++;
        }

        lines_processed++;
        text += len;

        // Skip whitespace at line start
        while (*text == ' ' || *text == '\n') text++;
    }

    return lines_drawn;
}

int count_words(const char *text)
{
    if (!text) return 0;

    int count = 0;
    bool in_word = false;

    while (*text) {
        if (isspace((unsigned char)*text)) {
            in_word = false;
        } else {
            if (!in_word) {
                count++;
                in_word = true;
            }
        }
        text++;
    }

    return count;
}

char* truncate_at_words(const char *text, int max_words)
{
    if (!text || max_words <= 0) return NULL;

    int word_count = 0;
    bool in_word = false;
    const char *end = text;

    // Find position after max_words
    while (*end) {
        if (isspace((unsigned char)*end)) {
            in_word = false;
        } else {
            if (!in_word) {
                word_count++;
                if (word_count > max_words) {
                    break;
                }
                in_word = true;
            }
        }
        end++;
    }

    // Calculate length
    size_t len = end - text;
    bool needs_ellipsis = (*end != '\0');

    // Allocate result
    size_t result_len = len + (needs_ellipsis ? 4 : 1);  // +4 for "..." + null
    char *result = malloc(result_len);
    if (!result) return NULL;

    // Copy text
    memcpy(result, text, len);

    // Trim trailing whitespace before ellipsis
    while (len > 0 && isspace((unsigned char)result[len - 1])) {
        len--;
    }

    // Add ellipsis if truncated
    if (needs_ellipsis) {
        result[len] = '.';
        result[len + 1] = '.';
        result[len + 2] = '.';
        result[len + 3] = '\0';
    } else {
        result[len] = '\0';
    }

    return result;
}

int calculate_optimal_word_count(int pane_height, int pane_width, int font_size)
{
    // Named constants for clarity
    const int LINE_SPACING = 2;
    const int HEADER_PADDING = 8;
    const int VERTICAL_PADDING = 16;
    const int AVG_WORD_LENGTH = 6;      // 5 chars + 1 space
    const int SAFETY_MARGIN_PERCENT = 90;
    const int MIN_WORDS = 20;
    const int MAX_WORDS = 150;

    if (pane_height <= 0 || pane_width <= 0 || font_size <= 0) {
        return 0;
    }

    int line_height = font_size + LINE_SPACING;
    int header_height = font_size + HEADER_PADDING;
    int available_height = pane_height - header_height - VERTICAL_PADDING;

    if (available_height <= 0) return 0;

    int visible_lines = available_height / line_height;

    // Estimate characters per line (~0.6 * font_size for proportional fonts)
    int avg_char_width = (font_size * 6) / 10;
    if (avg_char_width < 1) avg_char_width = 1;

    int chars_per_line = pane_width / avg_char_width;
    int words_per_line = chars_per_line / AVG_WORD_LENGTH;
    if (words_per_line < 1) words_per_line = 1;

    int total_words = visible_lines * words_per_line;
    total_words = (total_words * SAFETY_MARGIN_PERCENT) / 100;

    // Clamp to reasonable range
    if (total_words < MIN_WORDS) return MIN_WORDS;
    if (total_words > MAX_WORDS) return MAX_WORDS;
    return total_words;
}

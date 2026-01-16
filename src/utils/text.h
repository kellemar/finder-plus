#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include "raylib.h"

// Draw text with word wrapping
// Returns the number of lines drawn
int draw_text_wrapped(const char *text, int x, int y, int max_width,
                      int max_lines, int font_size, Color color);

// Calculate how many lines text will take when wrapped
int measure_text_lines(const char *text, int max_width, int font_size);

// Draw text with word wrapping and scroll offset
// skip_lines: number of wrapped lines to skip (for scrolling)
// Returns the number of lines drawn
int draw_text_wrapped_scrolled(const char *text, int x, int y, int max_width,
                               int max_lines, int skip_lines, int font_size, Color color);

// Count the number of words in text
int count_words(const char *text);

// Truncate text at max_words and append "..."
// Returns malloc'd string that caller must free, or NULL on error
char* truncate_at_words(const char *text, int max_words);

// Calculate optimal word count for a given pane height and width
// Returns the number of words that will fit comfortably
int calculate_optimal_word_count(int pane_height, int pane_width, int font_size);

#endif // TEXT_UTILS_H

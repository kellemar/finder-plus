#include "search.h"
#include "../app.h"
#include "../ui/tabs.h"
#include "../ui/sidebar.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "../ai/semantic_search.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Search bar height
#define SEARCH_BAR_HEIGHT 32

void search_init(SearchState *search)
{
    memset(search, 0, sizeof(SearchState));
    search->mode = SEARCH_INACTIVE;
    search->fuzzy_enabled = true;
    search->case_sensitive = false;
    search->search_type = SEARCH_TYPE_FUZZY;
    search->semantic_available = false; // Will be set when app initializes AI
}

void search_start(SearchState *search)
{
    search->mode = SEARCH_TYPING;
    search->query[0] = '\0';
    search->cursor = 0;
    search->result_count = 0;
    search->selected_result = 0;
}

void search_stop(SearchState *search)
{
    search->mode = SEARCH_INACTIVE;
    search->query[0] = '\0';
    search->cursor = 0;
    search->result_count = 0;
    search->selected_result = 0;
}

void search_input_char(SearchState *search, char c)
{
    if (search->mode == SEARCH_INACTIVE) return;

    int len = strlen(search->query);
    if (len >= SEARCH_MAX_QUERY - 1) return;

    // Insert character at cursor position
    for (int i = len; i >= search->cursor; i--) {
        search->query[i + 1] = search->query[i];
    }
    search->query[search->cursor] = c;
    search->cursor++;
}

void search_input_backspace(SearchState *search)
{
    if (search->mode == SEARCH_INACTIVE) return;
    if (search->cursor <= 0) return;

    int len = strlen(search->query);
    for (int i = search->cursor - 1; i < len; i++) {
        search->query[i] = search->query[i + 1];
    }
    search->cursor--;
}

void search_cursor_left(SearchState *search)
{
    if (search->cursor > 0) {
        search->cursor--;
    }
}

void search_cursor_right(SearchState *search)
{
    int len = strlen(search->query);
    if (search->cursor < len) {
        search->cursor++;
    }
}

void search_next_result(SearchState *search)
{
    if (search->result_count > 0 && search->selected_result < search->result_count - 1) {
        search->selected_result++;
    }
}

void search_prev_result(SearchState *search)
{
    if (search->result_count > 0 && search->selected_result > 0) {
        search->selected_result--;
    }
}

// Compare function for sorting results by score
static int result_compare(const void *a, const void *b)
{
    const SearchResult *ra = (const SearchResult *)a;
    const SearchResult *rb = (const SearchResult *)b;
    // Higher score first
    return rb->score - ra->score;
}

int search_fuzzy_match(const char *query, const char *text, int *match_positions, int *match_count, bool case_sensitive)
{
    if (!query || !text || query[0] == '\0') {
        if (match_count) *match_count = 0;
        return 0;
    }

    int q_len = strlen(query);
    int t_len = strlen(text);
    int q_idx = 0;
    int t_idx = 0;
    int score = 0;
    int matches = 0;
    int consecutive_bonus = 0;
    int prev_match_idx = -2;

    while (q_idx < q_len && t_idx < t_len) {
        char q_char = case_sensitive ? query[q_idx] : tolower((unsigned char)query[q_idx]);
        char t_char = case_sensitive ? text[t_idx] : tolower((unsigned char)text[t_idx]);

        if (q_char == t_char) {
            // Matched
            if (match_positions && matches < 64) {
                match_positions[matches] = t_idx;
            }
            matches++;

            // Base score for match
            score += 10;

            // Bonus for consecutive matches
            if (t_idx == prev_match_idx + 1) {
                consecutive_bonus++;
                score += 5 * consecutive_bonus;
            } else {
                consecutive_bonus = 0;
            }

            // Bonus for match at start of word
            if (t_idx == 0 || text[t_idx - 1] == ' ' || text[t_idx - 1] == '_' ||
                text[t_idx - 1] == '-' || text[t_idx - 1] == '.') {
                score += 15;
            }

            // Bonus for uppercase in camelCase
            if (t_idx > 0 && isupper((unsigned char)text[t_idx]) &&
                islower((unsigned char)text[t_idx - 1])) {
                score += 10;
            }

            prev_match_idx = t_idx;
            q_idx++;
        }
        t_idx++;
    }

    if (match_count) *match_count = matches;

    // Must match all query characters
    if (q_idx < q_len) {
        return 0;
    }

    // Bonus for shorter text (better match density)
    if (t_len > 0) {
        score += (100 * matches) / t_len;
    }

    return score;
}

void search_perform(SearchState *search, DirectoryState *dir)
{
    search->result_count = 0;
    search->selected_result = 0;

    if (search->query[0] == '\0') {
        return;
    }

    for (int i = 0; i < dir->count && search->result_count < SEARCH_MAX_RESULTS; i++) {
        FileEntry *entry = &dir->entries[i];

        int match_positions[64];
        int match_count = 0;

        int score;
        if (search->fuzzy_enabled) {
            score = search_fuzzy_match(search->query, entry->name,
                                       match_positions, &match_count,
                                       search->case_sensitive);
        } else {
            // Exact substring match
            const char *found = NULL;
            if (search->case_sensitive) {
                found = strstr(entry->name, search->query);
            } else {
                // Case-insensitive search
                char lower_name[NAME_MAX_LEN];
                char lower_query[SEARCH_MAX_QUERY];
                strncpy(lower_name, entry->name, NAME_MAX_LEN - 1);
                lower_name[NAME_MAX_LEN - 1] = '\0';
                strncpy(lower_query, search->query, SEARCH_MAX_QUERY - 1);
                lower_query[SEARCH_MAX_QUERY - 1] = '\0';

                for (int j = 0; lower_name[j]; j++) {
                    lower_name[j] = tolower((unsigned char)lower_name[j]);
                }
                for (int j = 0; lower_query[j]; j++) {
                    lower_query[j] = tolower((unsigned char)lower_query[j]);
                }

                found = strstr(lower_name, lower_query);
            }

            if (found) {
                score = 100;
                match_count = strlen(search->query);
                size_t offset = found - entry->name;
                for (int j = 0; j < match_count && j < 64; j++) {
                    match_positions[j] = (int)(offset + j);
                }
            } else {
                score = 0;
            }
        }

        if (score > 0) {
            SearchResult *result = &search->results[search->result_count];
            result->original_index = i;
            result->score = score;
            result->match_count = match_count;
            memcpy(result->match_positions, match_positions, sizeof(match_positions));
            search->result_count++;
        }
    }

    // Sort results by score (highest first)
    if (search->result_count > 1) {
        qsort(search->results, search->result_count, sizeof(SearchResult), result_compare);
    }
}

int search_get_selected_index(SearchState *search)
{
    if (search->result_count == 0) return -1;
    if (search->selected_result >= search->result_count) return -1;
    return search->results[search->selected_result].original_index;
}

bool search_is_active(SearchState *search)
{
    return search->mode != SEARCH_INACTIVE;
}

// Helper to perform search based on current type
static void search_perform_current(struct App *app)
{
    SearchState *search = &app->search;
    if (search->search_type == SEARCH_TYPE_SEMANTIC && search->semantic_available) {
        search_perform_semantic(app, search->query);
    } else {
        search_perform(search, &app->directory);
    }
}

void search_handle_input(struct App *app)
{
    SearchState *search = &app->search;

    // Update semantic availability
    search->semantic_available = search_is_semantic_available(app);

    // Start search: /
    if (!search_is_active(search) && IsKeyPressed(KEY_SLASH)) {
        search_start(search);
        return;
    }

    if (!search_is_active(search)) return;

    // Exit search: Escape
    if (IsKeyPressed(KEY_ESCAPE)) {
        search_stop(search);
        return;
    }

    // Toggle search type: Tab
    if (IsKeyPressed(KEY_TAB)) {
        search_toggle_type(search);
        // Re-perform search with new type
        if (search->query[0] != '\0') {
            search_perform_current(app);
        }
        return;
    }

    // Confirm selection: Enter
    if (IsKeyPressed(KEY_ENTER)) {
        int selected = search_get_selected_index(search);
        if (selected >= 0) {
            app->selected_index = selected;
            selection_clear(&app->selection);
        }
        search_stop(search);
        return;
    }

    // Navigate results: n/N or Ctrl+n/Ctrl+p
    bool ctrl_down = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    if (IsKeyPressed(KEY_N)) {
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
            search_prev_result(search);
        } else {
            search_next_result(search);
        }
        // Update selection to follow result
        int selected = search_get_selected_index(search);
        if (selected >= 0) {
            app->selected_index = selected;
        }
        return;
    }

    if (ctrl_down && IsKeyPressed(KEY_P)) {
        search_prev_result(search);
        int selected = search_get_selected_index(search);
        if (selected >= 0) {
            app->selected_index = selected;
        }
        return;
    }

    // Arrow keys for navigating results
    if (IsKeyPressed(KEY_DOWN)) {
        search_next_result(search);
        int selected = search_get_selected_index(search);
        if (selected >= 0) {
            app->selected_index = selected;
        }
        return;
    }

    if (IsKeyPressed(KEY_UP)) {
        search_prev_result(search);
        int selected = search_get_selected_index(search);
        if (selected >= 0) {
            app->selected_index = selected;
        }
        return;
    }

    // Cursor movement
    if (IsKeyPressed(KEY_LEFT)) {
        search_cursor_left(search);
        return;
    }

    if (IsKeyPressed(KEY_RIGHT)) {
        search_cursor_right(search);
        return;
    }

    // Backspace
    if (IsKeyPressed(KEY_BACKSPACE)) {
        search_input_backspace(search);
        search_perform_current(app);
        return;
    }

    // Text input
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key <= 126) {
            search_input_char(search, (char)key);
            search_perform_current(app);
        }
        key = GetCharPressed();
    }
}

void search_draw(struct App *app)
{
    SearchState *search = &app->search;

    if (!search_is_active(search)) return;

    int sidebar_width = app->sidebar.collapsed ? 0 : app->sidebar.width;
    int tab_height = tabs_get_height(&app->tabs);
    int content_width = app->width - sidebar_width;

    // Draw search bar at top of content area (below tabs)
    int bar_x = sidebar_width;
    int bar_y = tab_height;
    int bar_height = SEARCH_BAR_HEIGHT;

    // Background
    DrawRectangle(bar_x, bar_y, content_width, bar_height, g_theme.sidebar);
    DrawLine(bar_x, bar_y + bar_height - 1, bar_x + content_width, bar_y + bar_height - 1, g_theme.border);

    // Search icon/label
    int text_x = bar_x + PADDING;
    int text_y = bar_y + (bar_height - FONT_SIZE) / 2;
    DrawTextCustom("/", text_x, text_y, FONT_SIZE, g_theme.accent);
    text_x += 16;

    // Query text
    DrawTextCustom(search->query, text_x, text_y, FONT_SIZE, g_theme.textPrimary);

    // Cursor
    float blink = GetTime();
    if ((int)(blink * 2) % 2 == 0) {
        int cursor_x = text_x + MeasureTextCustom(search->query, FONT_SIZE);
        if (search->cursor < (int)strlen(search->query)) {
            char temp[SEARCH_MAX_QUERY];
            strncpy(temp, search->query, search->cursor);
            temp[search->cursor] = '\0';
            cursor_x = text_x + MeasureTextCustom(temp, FONT_SIZE);
        }
        DrawRectangle(cursor_x, text_y, 2, FONT_SIZE, g_theme.accent);
    }

    // Result count and search type indicator
    if (search->query[0] != '\0') {
        char count_str[64];
        snprintf(count_str, sizeof(count_str), "%d/%d", search->selected_result + 1, search->result_count);
        int count_width = MeasureTextCustom(count_str, FONT_SIZE_SMALL);
        DrawTextCustom(count_str, bar_x + content_width - count_width - PADDING, text_y + 2, FONT_SIZE_SMALL, g_theme.textSecondary);
    }

    // Draw search type indicator (Tab to toggle)
    const char *type_label = search->search_type == SEARCH_TYPE_SEMANTIC ? "[AI]" : "[Fuzzy]";
    Color type_color = search->search_type == SEARCH_TYPE_SEMANTIC ? g_theme.aiAccent : g_theme.textSecondary;
    int type_width = MeasureTextCustom(type_label, FONT_SIZE_SMALL);
    int type_x = bar_x + content_width - type_width - PADDING - 80;
    DrawTextCustom(type_label, type_x, text_y + 2, FONT_SIZE_SMALL, type_color);
}

void search_toggle_type(SearchState *search)
{
    if (!search->semantic_available) {
        // Can't toggle to semantic if not available
        return;
    }
    if (search->search_type == SEARCH_TYPE_FUZZY) {
        search->search_type = SEARCH_TYPE_SEMANTIC;
    } else {
        search->search_type = SEARCH_TYPE_FUZZY;
    }
}

const char* search_type_name(SearchType type)
{
    switch (type) {
        case SEARCH_TYPE_FUZZY: return "Fuzzy";
        case SEARCH_TYPE_SEMANTIC: return "Semantic";
        default: return "Unknown";
    }
}

bool search_is_semantic_available(struct App *app)
{
    if (!app) return false;
    if (!app->ai_enabled) return false;
    if (!app->semantic_search) return false;
    return semantic_search_is_ready(app->semantic_search);
}

void search_perform_semantic(struct App *app, const char *query)
{
    if (!app || !query || query[0] == '\0') return;
    if (!app->semantic_search) return;

    SearchState *search = &app->search;
    search->result_count = 0;
    search->selected_result = 0;

    // Perform semantic search
    SemanticSearchOptions opts = semantic_search_default_options();
    opts.max_results = SEARCH_MAX_RESULTS;
    opts.min_score = 0.1f;  // Minimum similarity threshold
    opts.directory = app->directory.current_path;  // Limit to current directory

    SemanticSearchResults results = semantic_search_query(app->semantic_search, query, &opts);

    if (results.success && results.count > 0) {
        // Convert semantic results to search results
        for (int i = 0; i < results.count && search->result_count < SEARCH_MAX_RESULTS; i++) {
            SemanticSearchResult *sem_result = &results.results[i];

            // Find the matching file in the current directory
            for (int j = 0; j < app->directory.count; j++) {
                FileEntry *entry = &app->directory.entries[j];
                if (strcmp(entry->path, sem_result->path) == 0) {
                    SearchResult *result = &search->results[search->result_count];
                    result->original_index = j;
                    result->score = (int)(sem_result->score * 1000);  // Convert to int score
                    result->match_count = 0;  // No character-level matching for semantic
                    search->result_count++;
                    break;
                }
            }
        }
    }

    semantic_search_results_free(&results);
}

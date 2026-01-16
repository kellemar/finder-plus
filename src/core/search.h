#ifndef SEARCH_H
#define SEARCH_H

#include "filesystem.h"
#include <stdbool.h>

#define SEARCH_MAX_QUERY 256
#define SEARCH_MAX_RESULTS 1024

// Search mode state
typedef enum SearchMode {
    SEARCH_INACTIVE,
    SEARCH_TYPING,
    SEARCH_RESULTS
} SearchMode;

// Search result with match score
typedef struct SearchResult {
    int original_index;     // Index in the directory entries
    int score;              // Match score (higher is better)
    int match_positions[64]; // Positions of matched characters
    int match_count;        // Number of matched positions
} SearchResult;

// Search type enum
typedef enum SearchType {
    SEARCH_TYPE_FUZZY,       // Filename fuzzy matching (default)
    SEARCH_TYPE_SEMANTIC     // Semantic content search (AI-powered)
} SearchType;

// Search state
typedef struct SearchState {
    SearchMode mode;
    char query[SEARCH_MAX_QUERY];
    int cursor;              // Cursor position in query

    SearchResult results[SEARCH_MAX_RESULTS];
    int result_count;
    int selected_result;     // Currently selected result index

    bool case_sensitive;
    bool fuzzy_enabled;      // Use fuzzy matching
    SearchType search_type;  // Current search type (fuzzy vs semantic)
    bool semantic_available; // Whether semantic search is available
} SearchState;

// Forward declaration
struct App;

// Initialize search state
void search_init(SearchState *search);

// Start search mode
void search_start(SearchState *search);

// Stop search mode and clear query
void search_stop(SearchState *search);

// Handle character input
void search_input_char(SearchState *search, char c);

// Handle backspace
void search_input_backspace(SearchState *search);

// Move cursor left
void search_cursor_left(SearchState *search);

// Move cursor right
void search_cursor_right(SearchState *search);

// Navigate to next result
void search_next_result(SearchState *search);

// Navigate to previous result
void search_prev_result(SearchState *search);

// Perform search on directory entries
void search_perform(SearchState *search, DirectoryState *dir);

// Calculate fuzzy match score between query and text
// Returns score (higher is better), or 0 if no match
int search_fuzzy_match(const char *query, const char *text, int *match_positions, int *match_count, bool case_sensitive);

// Get selected file index (original index in directory)
int search_get_selected_index(SearchState *search);

// Handle search input
void search_handle_input(struct App *app);

// Draw search bar
void search_draw(struct App *app);

// Check if search is active
bool search_is_active(SearchState *search);

// Toggle between fuzzy and semantic search types
void search_toggle_type(SearchState *search);

// Get current search type name
const char* search_type_name(SearchType type);

// Check if semantic search is available (engine loaded, etc.)
bool search_is_semantic_available(struct App *app);

// Perform semantic search (AI-powered content search)
void search_perform_semantic(struct App *app, const char *query);

#endif // SEARCH_H

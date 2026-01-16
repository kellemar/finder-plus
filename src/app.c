#include "app.h"
#include "core/search.h"
#include "ui/browser.h"
#include "ui/statusbar.h"
#include "ui/sidebar.h"
#include "ui/tabs.h"
#include "ui/preview.h"
#include "ui/breadcrumb.h"
#include "ui/dialog.h"
#include "ui/dual_pane.h"
#include "utils/config.h"
#include "utils/font.h"
#include "utils/perf.h"
#include "ai/embeddings.h"
#include "ai/vectordb.h"
#include "ai/clip.h"
#include "ai/indexer.h"
#include "ai/semantic_search.h"
#include "ai/visual_search.h"
#include "api/gemini_client.h"
#include "api/claude_client.h"
#include "ui/progress_indicator.h"
#include "ui/file_view_modal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

// Time window for gg command (in seconds)
#define GG_TIMEOUT 0.5f

// Grid view constants
#define GRID_ITEM_WIDTH 100
#define GRID_ITEM_HEIGHT 90

// Forward declaration
static void app_update_git_status(App *app);

// Initialize AI subsystem components
static void ai_subsystem_init(App *app)
{
    // Get database path from config or use default
    const char *home = getenv("HOME");
    char db_path[4096];
    snprintf(db_path, sizeof(db_path), "%s/.config/finder-plus/index.db",
             home ? home : "/tmp");

    // Initialize core components
    app->vectordb = vectordb_open(db_path);
    app->embedding_engine = embedding_engine_create();
    app->clip_engine = clip_engine_create();
    app->indexer = indexer_create();

    // Load AI models
    if (app->embedding_engine) {
        EmbeddingStatus emb_status = embedding_engine_load_model(
            app->embedding_engine, "models/all-MiniLM-L6-v2/ggml-model-q4_0.bin");
        if (emb_status == EMBEDDING_STATUS_OK) {
            TraceLog(LOG_INFO, "Loaded embedding model");
        }
    }
    if (app->clip_engine) {
        CLIPStatus clip_status = clip_engine_load_model(
            app->clip_engine, "models/clip-vit-b32.gguf");
        if (clip_status == CLIP_STATUS_OK) {
            TraceLog(LOG_INFO, "Loaded CLIP model");
        }
    }

    // Configure indexer if all core components available
    if (app->indexer && app->embedding_engine && app->vectordb) {
        indexer_set_embedding_engine(app->indexer, app->embedding_engine);
        indexer_set_vectordb(app->indexer, app->vectordb);
        const char *excludes[] = {"node_modules", ".git", ".DS_Store", "*.pyc", "__pycache__"};
        for (int i = 0; i < 5; i++) {
            indexer_add_exclude_pattern(app->indexer, excludes[i]);
        }
    }

    // Initialize semantic search
    app->semantic_search = semantic_search_create();
    if (app->semantic_search) {
        semantic_search_set_embedding_engine(app->semantic_search, app->embedding_engine);
        semantic_search_set_vectordb(app->semantic_search, app->vectordb);
        semantic_search_set_indexer(app->semantic_search, app->indexer);
    }

    // Initialize visual search
    app->visual_search = visual_search_create();
    if (app->visual_search) {
        visual_search_set_clip_engine(app->visual_search, app->clip_engine);
        visual_search_set_vectordb(app->visual_search, app->vectordb);
    }
}

// Free AI subsystem components
static void ai_subsystem_free(App *app)
{
    // Stop indexer first (it may be writing to vectordb)
    if (app->indexer) {
        indexer_stop(app->indexer);
        indexer_destroy(app->indexer);
        app->indexer = NULL;
    }

    // Free search components (they depend on engines)
    if (app->visual_search) {
        visual_search_destroy(app->visual_search);
        app->visual_search = NULL;
    }
    if (app->semantic_search) {
        semantic_search_destroy(app->semantic_search);
        app->semantic_search = NULL;
    }

    // Free engines
    if (app->clip_engine) {
        clip_engine_destroy(app->clip_engine);
        app->clip_engine = NULL;
    }
    if (app->embedding_engine) {
        embedding_engine_destroy(app->embedding_engine);
        app->embedding_engine = NULL;
    }

    // Close database last
    if (app->vectordb) {
        vectordb_close(app->vectordb);
        app->vectordb = NULL;
    }
}

// Callback for delete confirmation dialog
static void perform_delete_confirmed(App *app)
{
    if (app->directory.count == 0) return;

    // Delete selected items
    if (app->selection.count > 0) {
        for (int i = 0; i < app->selection.count; i++) {
            file_delete(app->directory.entries[app->selection.indices[i]].path);
        }
    } else {
        file_delete(app->directory.entries[app->selected_index].path);
    }

    // Refresh directory
    directory_read(&app->directory, app->directory.current_path);
    selection_clear(&app->selection);
    app_update_git_status(app);

    if (app->selected_index >= app->directory.count) {
        app->selected_index = app->directory.count > 0 ? app->directory.count - 1 : 0;
    }
}

// Selection functions implementation
void selection_init(SelectionState *sel)
{
    sel->indices = NULL;
    sel->count = 0;
    sel->capacity = 0;
    sel->anchor_index = -1;
}

void selection_free(SelectionState *sel)
{
    if (sel->indices) {
        free(sel->indices);
        sel->indices = NULL;
    }
    sel->count = 0;
    sel->capacity = 0;
    sel->anchor_index = -1;
}

void selection_clear(SelectionState *sel)
{
    sel->count = 0;
    sel->anchor_index = -1;
}

static bool selection_ensure_capacity(SelectionState *sel, int needed)
{
    if (needed <= sel->capacity) {
        return true;
    }

    int new_capacity = sel->capacity == 0 ? 16 : sel->capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    if (new_capacity > MAX_SELECTION) {
        new_capacity = MAX_SELECTION;
    }

    int *new_indices = realloc(sel->indices, new_capacity * sizeof(int));
    if (!new_indices) {
        return false;
    }

    sel->indices = new_indices;
    sel->capacity = new_capacity;
    return true;
}

void selection_add(SelectionState *sel, int index)
{
    // Check if already selected
    for (int i = 0; i < sel->count; i++) {
        if (sel->indices[i] == index) {
            return;
        }
    }

    if (!selection_ensure_capacity(sel, sel->count + 1)) {
        return;
    }

    sel->indices[sel->count] = index;
    sel->count++;
}

void selection_remove(SelectionState *sel, int index)
{
    for (int i = 0; i < sel->count; i++) {
        if (sel->indices[i] == index) {
            // Shift remaining elements
            for (int j = i; j < sel->count - 1; j++) {
                sel->indices[j] = sel->indices[j + 1];
            }
            sel->count--;
            return;
        }
    }
}

void selection_toggle(SelectionState *sel, int index)
{
    if (selection_contains(sel, index)) {
        selection_remove(sel, index);
    } else {
        selection_add(sel, index);
    }
}

bool selection_contains(SelectionState *sel, int index)
{
    for (int i = 0; i < sel->count; i++) {
        if (sel->indices[i] == index) {
            return true;
        }
    }
    return false;
}

void selection_range(SelectionState *sel, int from, int to)
{
    selection_clear(sel);

    int start = from < to ? from : to;
    int end = from < to ? to : from;

    for (int i = start; i <= end; i++) {
        selection_add(sel, i);
    }
}

void selection_select_all(App *app)
{
    selection_clear(&app->selection);
    for (int i = 0; i < app->directory.count; i++) {
        selection_add(&app->selection, i);
    }
}

// Sidebar functions implementation
void sidebar_init(SidebarState *sidebar)
{
    sidebar->favorite_count = 0;
    sidebar->volume_count = 0;
    sidebar->width = SIDEBAR_DEFAULT_WIDTH;
    sidebar->collapsed = false;
    sidebar->resizing = false;
    sidebar->hovered_index = -1;

    // Add default favorites
    const char *home = getenv("HOME");
    if (home) {
        char path[PATH_MAX_LEN];

        // Home
        strncpy(sidebar->favorites[sidebar->favorite_count].name, "Home", 63);
        strncpy(sidebar->favorites[sidebar->favorite_count].path, home, PATH_MAX_LEN - 1);
        sidebar->favorites[sidebar->favorite_count].is_volume = false;
        sidebar->favorite_count++;

        // Desktop
        snprintf(path, sizeof(path), "%s/Desktop", home);
        strncpy(sidebar->favorites[sidebar->favorite_count].name, "Desktop", 63);
        strncpy(sidebar->favorites[sidebar->favorite_count].path, path, PATH_MAX_LEN - 1);
        sidebar->favorites[sidebar->favorite_count].is_volume = false;
        sidebar->favorite_count++;

        // Documents
        snprintf(path, sizeof(path), "%s/Documents", home);
        strncpy(sidebar->favorites[sidebar->favorite_count].name, "Documents", 63);
        strncpy(sidebar->favorites[sidebar->favorite_count].path, path, PATH_MAX_LEN - 1);
        sidebar->favorites[sidebar->favorite_count].is_volume = false;
        sidebar->favorite_count++;

        // Downloads
        snprintf(path, sizeof(path), "%s/Downloads", home);
        strncpy(sidebar->favorites[sidebar->favorite_count].name, "Downloads", 63);
        strncpy(sidebar->favorites[sidebar->favorite_count].path, path, PATH_MAX_LEN - 1);
        sidebar->favorites[sidebar->favorite_count].is_volume = false;
        sidebar->favorite_count++;
    }

    // Refresh volumes
    sidebar_refresh_volumes(sidebar);
}

void sidebar_refresh_volumes(SidebarState *sidebar)
{
    sidebar->volume_count = 0;

    // Add root
    strncpy(sidebar->volumes[sidebar->volume_count].name, "Macintosh HD", 63);
    strncpy(sidebar->volumes[sidebar->volume_count].path, "/", PATH_MAX_LEN - 1);
    sidebar->volumes[sidebar->volume_count].is_volume = true;
    sidebar->volume_count++;

    // Scan /Volumes for mounted volumes
    DIR *dir = opendir("/Volumes");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && sidebar->volume_count < 16) {
            if (entry->d_name[0] == '.') continue;
            if (strcmp(entry->d_name, "Macintosh HD") == 0) continue;

            char path[PATH_MAX_LEN];
            snprintf(path, sizeof(path), "/Volumes/%s", entry->d_name);

            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
                strncpy(sidebar->volumes[sidebar->volume_count].name, entry->d_name, 63);
                strncpy(sidebar->volumes[sidebar->volume_count].path, path, PATH_MAX_LEN - 1);
                sidebar->volumes[sidebar->volume_count].is_volume = true;
                sidebar->volume_count++;
            }
        }
        closedir(dir);
    }
}

// History functions implementation
void history_init(HistoryState *history)
{
    history->count = 0;
    history->current = -1;
}

void history_push(HistoryState *history, const char *path)
{
    // If we're not at the end of history, truncate
    if (history->current < history->count - 1) {
        history->count = history->current + 1;
    }

    // If we're at max capacity, shift everything
    if (history->count >= MAX_HISTORY) {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strncpy(history->paths[i], history->paths[i + 1], PATH_MAX_LEN - 1);
        }
        history->count--;
    }

    // Add new path
    strncpy(history->paths[history->count], path, PATH_MAX_LEN - 1);
    history->paths[history->count][PATH_MAX_LEN - 1] = '\0';
    history->count++;
    history->current = history->count - 1;
}

const char* history_back(HistoryState *history)
{
    if (history->current > 0) {
        history->current--;
        return history->paths[history->current];
    }
    return NULL;
}

const char* history_forward(HistoryState *history)
{
    if (history->current < history->count - 1) {
        history->current++;
        return history->paths[history->current];
    }
    return NULL;
}

bool history_can_go_back(HistoryState *history)
{
    return history->current > 0;
}

bool history_can_go_forward(HistoryState *history)
{
    return history->current < history->count - 1;
}

// View mode helpers
const char* view_mode_name(ViewMode mode)
{
    switch (mode) {
        case VIEW_LIST: return "List View";
        case VIEW_GRID: return "Grid View";
        case VIEW_COLUMN: return "Column View";
        default: return "Unknown";
    }
}

void app_init(App *app, const char *start_path)
{
    // Initialize theme (default to dark)
    theme_init(THEME_DARK);

    // Initialize custom font (falls back to default if not found)
    font_init("assets/fonts/cozette.ttf");

    // Window settings
    app->width = DEFAULT_WIDTH;
    app->height = DEFAULT_HEIGHT;
    app->fullscreen = false;
    app->should_close = false;

    // View mode - load from config if available
    app->view_mode = (ViewMode)g_config.appearance.view_mode;

    // Browser state
    directory_state_init(&app->directory);
    app->selected_index = 0;
    app->scroll_offset = 0;
    app->visible_rows = 0;

    // Multi-selection
    selection_init(&app->selection);

    // Sidebar
    sidebar_init(&app->sidebar);

    // Column view
    memset(&app->columns, 0, sizeof(app->columns));

    // History
    history_init(&app->history);

    // Grid view
    app->grid_cols = 8;
    app->grid_rows = 0;

    // Rubber band selection
    app->rubber_band_active = false;
    app->rubber_band_start = (Vector2){0, 0};
    app->rubber_band_current = (Vector2){0, 0};

    // Browser mouse/hover state
    app->browser_state.hovered_index = -1;
    app->browser_state.hover_start_time = 0.0;
    app->browser_state.last_click_index = -1;
    app->browser_state.last_click_time = 0.0;
    app->browser_state.summary_state = HOVER_IDLE;
    app->browser_state.summary_text[0] = '\0';
    app->browser_state.summary_path[0] = '\0';
    app->browser_state.summary_error[0] = '\0';

    // Async summary threading
    pthread_mutex_init(&app->summary_mutex, NULL);
    atomic_store(&app->summary_thread_active, false);

    // Summary system
    summarize_config_init(&app->summary_config);

    // Load API key for summarization from environment variable
    const char *api_key = getenv("CLAUDE_API_KEY");
    if (api_key && strlen(api_key) > 0) {
        strncpy(app->summary_config.api_key, api_key, sizeof(app->summary_config.api_key) - 1);
        app->summary_config.api_key[sizeof(app->summary_config.api_key) - 1] = '\0';
    }

    summarize_async_init(&app->async_summary_request, &app->summary_mutex);
    app->summary_cache = summary_cache_create(NULL);  // Uses default path
    progress_indicator_init(&app->summary_progress, PROGRESS_SPINNER);
    progress_indicator_set_message(&app->summary_progress, "Summarizing...");

    // Input state
    app->g_pressed = false;
    app->g_timer = 0.0f;

    // Rename mode
    app->rename_mode = false;
    app->rename_buffer[0] = '\0';
    app->rename_cursor = 0;
    app->rename_index = -1;

    // Clipboard
    clipboard_init(&app->clipboard);

    // Tabs
    tabs_init(&app->tabs);

    // Search
    search_init(&app->search);

    // Preview
    preview_init(&app->preview);

    // Breadcrumb
    breadcrumb_init(&app->breadcrumb);

    // Git integration
    git_state_init(&app->git);
    git_status_result_init(&app->git_status);
    app->git_enabled = true;

    // Operation queue
    operation_queue_init(&app->op_queue);
    operation_queue_start(&app->op_queue);
    queue_panel_init(&app->queue_panel);

    // Command palette
    palette_init(&app->palette);
    palette_register_builtins(&app->palette);

    // Custom keybindings
    keybindings_init(&app->keybindings);

    // Dialog system
    dialog_init(&app->dialog);

    // File view modal (double-click to view full content)
    file_view_modal_init(&app->file_view_modal);

    // Context menu
    context_menu_init(&app->context_menu);

    // Dual pane mode (Phase 8)
    dual_pane_init(&app->dual_pane);

    // Network locations (Phase 8)
    network_init(&app->network);
    network_load_profiles(&app->network);

    // AI Command bar (Cmd+K)
    command_bar_init(&app->command_bar);
    command_bar_load_auth(&app->command_bar, NULL);
    command_bar_load_gemini_auth(&app->command_bar, NULL);

    // Local AI (Phase 5)
    app->ai_enabled = true;
    app->ai_indexing = false;
    ai_subsystem_init(app);

    // Connect AI search to command bar
    command_bar_set_semantic_search(&app->command_bar, app->semantic_search);
    command_bar_set_visual_search(&app->command_bar, app->visual_search);

    // Performance (Phase 8)
    perf_init(&app->perf);
    app->fps = 0.0f;
    app->show_perf_stats = false;

    // Text edit state (Phase 8 - context menu)
    app->text_edit_state = TEXT_EDIT_NONE;
    app->text_edit_path[0] = '\0';
    app->text_edit_prompt[0] = '\0';
    app->text_edit_cursor = 0;
    app->text_edit_result_path[0] = '\0';
    app->text_edit_error[0] = '\0';
    progress_indicator_init(&app->text_edit_progress, PROGRESS_SPINNER);
    progress_indicator_set_message(&app->text_edit_progress, "Editing with AI...");

    // Load initial directory
    const char *path = start_path;
    if (!path || path[0] == '\0') {
        path = getenv("HOME");
        if (!path) {
            path = "/";
        }
    }

    if (!directory_read(&app->directory, path)) {
        // Fall back to home directory if provided path fails
        const char *home = getenv("HOME");
        if (home && strcmp(path, home) != 0) {
            directory_read(&app->directory, home);
        }
    }

    // Update git status for initial directory
    app_update_git_status(app);

    // Push initial path to history
    history_push(&app->history, app->directory.current_path);

    // Create first tab
    tabs_new(&app->tabs, app->directory.current_path);

    // Update breadcrumb with initial path
    breadcrumb_update(&app->breadcrumb, app->directory.current_path);
}

void app_free(App *app)
{
    directory_state_free(&app->directory);
    selection_free(&app->selection);
    preview_free(&app->preview);
    file_view_modal_free(&app->file_view_modal);
    git_state_free(&app->git);
    git_status_result_free(&app->git_status);
    operation_queue_free(&app->op_queue);
    palette_free(&app->palette);
    keybindings_free(&app->keybindings);
    dual_pane_free(&app->dual_pane);
    network_shutdown(&app->network);
    command_bar_free(&app->command_bar);

    // Free custom font
    font_free();

    // Clean up Local AI (Phase 5)
    ai_subsystem_free(app);

    // Clean up async summary thread
    if (atomic_load(&app->summary_thread_active)) {
        summarize_async_cancel(&app->async_summary_request);
        pthread_join(app->summary_thread, NULL);
        atomic_store(&app->summary_thread_active, false);
    }
    pthread_mutex_destroy(&app->summary_mutex);

    // Clean up summary cache
    if (app->summary_cache) {
        summary_cache_destroy(app->summary_cache);
        app->summary_cache = NULL;
    }

    perf_free(&app->perf);
}

// Update git status for the current directory
static void app_update_git_status(App *app)
{
    if (!app->git_enabled) {
        app->git.is_repo = false;
        return;
    }

    // Update repository state
    git_update_state(&app->git, app->directory.current_path);

    // Get file statuses
    git_status_result_free(&app->git_status);
    git_status_result_init(&app->git_status);

    if (app->git.is_repo) {
        git_get_status(app->directory.current_path, &app->git_status);

        // Update git_status for each file entry
        for (int i = 0; i < app->directory.count; i++) {
            FileEntry *entry = &app->directory.entries[i];
            GitFileStatus status = git_get_file_status(&app->git_status, entry->name);

            // Map GitFileStatus to FileGitStatus
            switch (status) {
                case GIT_STATUS_UNTRACKED: entry->git_status = FILE_GIT_UNTRACKED; break;
                case GIT_STATUS_MODIFIED:  entry->git_status = FILE_GIT_MODIFIED; break;
                case GIT_STATUS_STAGED:    entry->git_status = FILE_GIT_STAGED; break;
                case GIT_STATUS_DELETED:   entry->git_status = FILE_GIT_DELETED; break;
                case GIT_STATUS_RENAMED:   entry->git_status = FILE_GIT_RENAMED; break;
                case GIT_STATUS_CONFLICT:  entry->git_status = FILE_GIT_CONFLICT; break;
                case GIT_STATUS_IGNORED:   entry->git_status = FILE_GIT_IGNORED; break;
                default: entry->git_status = FILE_GIT_NONE; break;
            }
        }
    } else {
        // Clear git status for all entries
        for (int i = 0; i < app->directory.count; i++) {
            app->directory.entries[i].git_status = FILE_GIT_NONE;
        }
    }
}

// Helper to enter rename mode
static void app_enter_rename_mode(App *app)
{
    if (app->directory.count == 0) return;

    FileEntry *entry = &app->directory.entries[app->selected_index];
    app->rename_mode = true;
    app->rename_index = app->selected_index;
    strncpy(app->rename_buffer, entry->name, NAME_MAX_LEN - 1);
    app->rename_buffer[NAME_MAX_LEN - 1] = '\0';
    app->rename_cursor = (int)strlen(app->rename_buffer);
}

// Helper to exit rename mode
static void app_exit_rename_mode(App *app, bool apply)
{
    if (!app->rename_mode) return;

    if (apply && app->rename_index >= 0 && app->rename_index < app->directory.count) {
        // Apply rename if name changed
        FileEntry *entry = &app->directory.entries[app->rename_index];
        if (strcmp(entry->name, app->rename_buffer) != 0 && app->rename_buffer[0] != '\0') {
            OperationResult result = file_rename(entry->path, app->rename_buffer);
            if (result == OP_SUCCESS) {
                // Refresh directory to show new name
                directory_read(&app->directory, app->directory.current_path);
                app_update_git_status(app);
            }
        }
    }

    app->rename_mode = false;
    app->rename_index = -1;
    app->rename_buffer[0] = '\0';
    app->rename_cursor = 0;
}

// Handle rename mode input - returns true if input was consumed
static bool app_handle_rename_input(App *app)
{
    if (!app->rename_mode) return false;

    // Escape cancels rename
    if (IsKeyPressed(KEY_ESCAPE)) {
        app_exit_rename_mode(app, false);
        return true;
    }

    // Enter confirms rename
    if (IsKeyPressed(KEY_ENTER)) {
        app_exit_rename_mode(app, true);
        return true;
    }

    // Tab also confirms (like Finder)
    if (IsKeyPressed(KEY_TAB)) {
        app_exit_rename_mode(app, true);
        return true;
    }

    // Backspace
    if (IsKeyPressed(KEY_BACKSPACE)) {
        if (app->rename_cursor > 0) {
            int len = (int)strlen(app->rename_buffer);
            for (int i = app->rename_cursor - 1; i < len; i++) {
                app->rename_buffer[i] = app->rename_buffer[i + 1];
            }
            app->rename_cursor--;
        }
        return true;
    }

    // Delete key
    if (IsKeyPressed(KEY_DELETE)) {
        int len = (int)strlen(app->rename_buffer);
        if (app->rename_cursor < len) {
            for (int i = app->rename_cursor; i < len; i++) {
                app->rename_buffer[i] = app->rename_buffer[i + 1];
            }
        }
        return true;
    }

    // Left arrow
    if (IsKeyPressed(KEY_LEFT)) {
        if (app->rename_cursor > 0) {
            app->rename_cursor--;
        }
        return true;
    }

    // Right arrow
    if (IsKeyPressed(KEY_RIGHT)) {
        int len = (int)strlen(app->rename_buffer);
        if (app->rename_cursor < len) {
            app->rename_cursor++;
        }
        return true;
    }

    // Home (Cmd+Left on macOS)
    bool cmd_down = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (cmd_down && IsKeyPressed(KEY_LEFT)) {
        app->rename_cursor = 0;
        return true;
    }

    // End (Cmd+Right on macOS)
    if (cmd_down && IsKeyPressed(KEY_RIGHT)) {
        app->rename_cursor = (int)strlen(app->rename_buffer);
        return true;
    }

    // Select all: Cmd+A
    if (cmd_down && IsKeyPressed(KEY_A)) {
        app->rename_cursor = (int)strlen(app->rename_buffer);
        return true;
    }

    // Text input
    int key = GetCharPressed();
    while (key > 0) {
        // Only allow printable characters, but not / (path separator)
        if (key >= 32 && key <= 126 && key != '/') {
            int len = (int)strlen(app->rename_buffer);
            if (len < NAME_MAX_LEN - 1) {
                // Insert character at cursor
                for (int i = len; i >= app->rename_cursor; i--) {
                    app->rename_buffer[i + 1] = app->rename_buffer[i];
                }
                app->rename_buffer[app->rename_cursor] = (char)key;
                app->rename_cursor++;
            }
        }
        key = GetCharPressed();
    }

    return true; // Consume all input while in rename mode
}

// Handle text edit input mode - returns true if input was consumed
static bool app_handle_text_edit_input(App *app)
{
    if (app->text_edit_state == TEXT_EDIT_NONE) return false;

    // Handle success/error states - dismiss on any key
    if (app->text_edit_state == TEXT_EDIT_SUCCESS ||
        app->text_edit_state == TEXT_EDIT_ERROR) {
        int key = GetKeyPressed();
        if (key != 0 || IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            app->text_edit_state = TEXT_EDIT_NONE;
            app->text_edit_path[0] = '\0';
            app->text_edit_prompt[0] = '\0';
            app->text_edit_error[0] = '\0';
        }
        return true;  // Block input while showing result
    }

    // Loading state - just block input
    if (app->text_edit_state == TEXT_EDIT_LOADING) {
        return true;
    }

    // Input state handling
    if (app->text_edit_state != TEXT_EDIT_INPUT) return false;

    // Escape cancels edit
    if (IsKeyPressed(KEY_ESCAPE)) {
        app->text_edit_state = TEXT_EDIT_NONE;
        app->text_edit_path[0] = '\0';
        app->text_edit_prompt[0] = '\0';
        return true;
    }

    // Enter submits (if prompt not empty)
    if (IsKeyPressed(KEY_ENTER) && strlen(app->text_edit_prompt) > 0) {
        app->text_edit_state = TEXT_EDIT_LOADING;
        return true;
    }

    // Backspace
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        if (app->text_edit_cursor > 0) {
            int len = (int)strlen(app->text_edit_prompt);
            for (int i = app->text_edit_cursor - 1; i < len; i++) {
                app->text_edit_prompt[i] = app->text_edit_prompt[i + 1];
            }
            app->text_edit_cursor--;
        }
        return true;
    }

    // Delete key
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) {
        int len = (int)strlen(app->text_edit_prompt);
        if (app->text_edit_cursor < len) {
            for (int i = app->text_edit_cursor; i < len; i++) {
                app->text_edit_prompt[i] = app->text_edit_prompt[i + 1];
            }
        }
        return true;
    }

    // Left/right arrows for cursor movement
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
        if (app->text_edit_cursor > 0) {
            app->text_edit_cursor--;
        }
        return true;
    }
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
        int len = (int)strlen(app->text_edit_prompt);
        if (app->text_edit_cursor < len) {
            app->text_edit_cursor++;
        }
        return true;
    }

    // Character input
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key <= 126) {  // Printable ASCII
            int len = (int)strlen(app->text_edit_prompt);
            if (len < (int)sizeof(app->text_edit_prompt) - 1) {
                // Insert at cursor
                for (int i = len; i >= app->text_edit_cursor; i--) {
                    app->text_edit_prompt[i + 1] = app->text_edit_prompt[i];
                }
                app->text_edit_prompt[app->text_edit_cursor] = (char)key;
                app->text_edit_cursor++;
            }
        }
        key = GetCharPressed();
    }

    return true;  // Consume all input while in text edit mode
}

void app_handle_input(App *app)
{
    // Handle dialog input first (modal, blocks everything else)
    if (dialog_handle_input(app)) {
        return; // Dialog is active, don't process other input
    }

    // Handle file view modal input (modal, blocks everything else)
    if (file_view_modal_handle_input(app)) {
        return; // File view modal is active, don't process other input
    }

    // Handle context menu input (high priority, blocks other input)
    if (context_menu_handle_input(app)) {
        return; // Context menu is active
    }

    // AI Command bar: Cmd+K to toggle
    bool cmd_down_early = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (cmd_down_early && IsKeyPressed(KEY_K)) {
        command_bar_toggle(&app->command_bar);
        command_bar_set_current_dir(&app->command_bar, app->directory.current_path);
        return;
    }

    // Handle command bar input when active (blocks other input)
    if (command_bar_is_active(&app->command_bar)) {
        command_bar_handle_input(&app->command_bar);
        return;
    }

    // Handle preview edit input mode (blocks other input when editing)
    if (preview_handle_input(app)) {
        return; // Preview edit mode is active, don't process other input
    }

    // Handle rename mode input (takes priority)
    if (app_handle_rename_input(app)) {
        return; // Rename mode is active
    }

    // Handle text edit input mode (takes priority)
    if (app_handle_text_edit_input(app)) {
        return; // Text edit mode is active
    }

    // Handle breadcrumb input first (edit mode takes priority)
    breadcrumb_handle_input(app);
    if (breadcrumb_is_editing(&app->breadcrumb)) {
        return; // Don't process other input while editing path
    }

    // Handle search input (takes priority when active)
    search_handle_input(app);
    if (search_is_active(&app->search)) {
        return; // Don't process other input while searching
    }

    float dt = GetFrameTime();
    bool cmd_down = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    bool shift_down = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    // Handle gg timeout
    if (app->g_pressed) {
        app->g_timer += dt;
        if (app->g_timer > GG_TIMEOUT) {
            app->g_pressed = false;
            app->g_timer = 0.0f;
        }
    }

    // Fullscreen toggle: Cmd+Enter (Super+Enter on macOS)
    if (cmd_down && IsKeyPressed(KEY_ENTER)) {
        app->fullscreen = !app->fullscreen;
        ToggleFullscreen();
    }

    // Toggle hidden files: Cmd+Shift+.
    if (cmd_down && shift_down && IsKeyPressed(KEY_PERIOD)) {
        directory_toggle_hidden(&app->directory);
        app->selected_index = 0;
        app->scroll_offset = 0;
        selection_clear(&app->selection);
    }

    // Toggle theme: Cmd+Shift+T
    if (cmd_down && shift_down && IsKeyPressed(KEY_T)) {
        theme_toggle();
    }

    // Toggle performance stats display: Cmd+Shift+P
    if (cmd_down && shift_down && IsKeyPressed(KEY_P)) {
        app->show_perf_stats = !app->show_perf_stats;
    }

    // View mode switching: Cmd+1/2/3
    if (cmd_down && IsKeyPressed(KEY_ONE)) {
        app->view_mode = VIEW_LIST;
        g_config.appearance.view_mode = CONFIG_VIEW_LIST;
        config_save(&g_config);
    }
    if (cmd_down && IsKeyPressed(KEY_TWO)) {
        app->view_mode = VIEW_GRID;
        g_config.appearance.view_mode = CONFIG_VIEW_GRID;
        config_save(&g_config);
    }
    if (cmd_down && IsKeyPressed(KEY_THREE)) {
        app->view_mode = VIEW_COLUMN;
        g_config.appearance.view_mode = CONFIG_VIEW_COLUMN;
        config_save(&g_config);
    }

    // Toggle dual pane mode: F3 or Cmd+Shift+D
    if (IsKeyPressed(KEY_F3) || (cmd_down && shift_down && IsKeyPressed(KEY_D))) {
        dual_pane_toggle(app);
    }

    // Handle dual pane input when active
    if (dual_pane_is_enabled(&app->dual_pane)) {
        dual_pane_handle_input(app);
        return; // Dual pane mode handles its own navigation
    }

    // Select all: Cmd+A
    if (cmd_down && IsKeyPressed(KEY_A)) {
        selection_select_all(app);
    }

    // Clear selection: Escape
    if (IsKeyPressed(KEY_ESCAPE)) {
        selection_clear(&app->selection);
    }

    // Enter rename mode: F2 or Enter on file (not directory)
    if (IsKeyPressed(KEY_F2)) {
        if (app->directory.count > 0) {
            app_enter_rename_mode(app);
        }
    }

    // Toggle sidebar: Cmd+backslash
    if (cmd_down && IsKeyPressed(KEY_BACKSLASH)) {
        app->sidebar.collapsed = !app->sidebar.collapsed;
    }

    // Navigation history: Cmd+[ for back, Cmd+] for forward
    if (cmd_down && IsKeyPressed(KEY_LEFT_BRACKET)) {
        const char *path = history_back(&app->history);
        if (path) {
            directory_read(&app->directory, path);
            app->selected_index = 0;
            app->scroll_offset = 0;
            selection_clear(&app->selection);
            breadcrumb_update(&app->breadcrumb, app->directory.current_path);
            app_update_git_status(app);
        }
    }
    if (cmd_down && IsKeyPressed(KEY_RIGHT_BRACKET)) {
        const char *path = history_forward(&app->history);
        if (path) {
            directory_read(&app->directory, path);
            app->selected_index = 0;
            app->scroll_offset = 0;
            selection_clear(&app->selection);
            breadcrumb_update(&app->breadcrumb, app->directory.current_path);
            app_update_git_status(app);
        }
    }

    // File operations

    // Copy: Cmd+C or yy
    if ((cmd_down && IsKeyPressed(KEY_C)) || (!cmd_down && !shift_down && IsKeyPressed(KEY_Y))) {
        if (app->directory.count > 0) {
            // Collect selected paths
            const char *paths[MAX_SELECTION];
            int path_count = 0;

            if (app->selection.count > 0) {
                for (int i = 0; i < app->selection.count && i < MAX_SELECTION; i++) {
                    paths[path_count++] = app->directory.entries[app->selection.indices[i]].path;
                }
            } else {
                paths[path_count++] = app->directory.entries[app->selected_index].path;
            }

            clipboard_copy(&app->clipboard, paths, path_count);
        }
    }

    // Cut: Cmd+X or dd
    if ((cmd_down && IsKeyPressed(KEY_X)) || (!cmd_down && !shift_down && IsKeyPressed(KEY_D))) {
        if (app->directory.count > 0) {
            const char *paths[MAX_SELECTION];
            int path_count = 0;

            if (app->selection.count > 0) {
                for (int i = 0; i < app->selection.count && i < MAX_SELECTION; i++) {
                    paths[path_count++] = app->directory.entries[app->selection.indices[i]].path;
                }
            } else {
                paths[path_count++] = app->directory.entries[app->selected_index].path;
            }

            clipboard_cut(&app->clipboard, paths, path_count);
        }
    }

    // Paste: Cmd+V or p
    if ((cmd_down && IsKeyPressed(KEY_V)) || (!cmd_down && !shift_down && IsKeyPressed(KEY_P))) {
        // Sync from system clipboard first (picks up files from other apps)
        clipboard_sync_from_system(&app->clipboard);

        if (clipboard_has_items(&app->clipboard)) {
            clipboard_paste(&app->clipboard, app->directory.current_path);
            // Refresh directory
            directory_read(&app->directory, app->directory.current_path);
            selection_clear(&app->selection);
            app_update_git_status(app);
        }
    }

    // Delete: Cmd+Backspace - show confirmation dialog
    if (cmd_down && IsKeyPressed(KEY_BACKSPACE)) {
        if (app->directory.count > 0) {
            // Build confirmation message
            char message[512];
            int count = (app->selection.count > 0) ? app->selection.count : 1;

            if (count == 1) {
                const char *name;
                if (app->selection.count > 0) {
                    name = app->directory.entries[app->selection.indices[0]].name;
                } else {
                    name = app->directory.entries[app->selected_index].name;
                }
                snprintf(message, sizeof(message),
                         "Move \"%s\" to Trash?", name);
            } else {
                snprintf(message, sizeof(message),
                         "Move %d items to Trash?", count);
            }

            dialog_confirm(&app->dialog, "Delete", message, perform_delete_confirmed);
        }
    }

    // New folder: Cmd+Shift+N
    if (cmd_down && shift_down && IsKeyPressed(KEY_N)) {
        // Create the folder and get its actual name (handles conflicts)
        char new_folder_path[PATH_MAX_LEN];
        snprintf(new_folder_path, sizeof(new_folder_path), "%s/untitled folder",
                 app->directory.current_path);

        // Generate unique name if needed
        char unique_path[PATH_MAX_LEN];
        generate_unique_name(new_folder_path, unique_path, sizeof(unique_path));

        // Get just the folder name from the unique path
        const char *new_name = strrchr(unique_path, '/');
        new_name = new_name ? new_name + 1 : unique_path;

        OperationResult result = file_create_directory(app->directory.current_path, new_name);
        if (result == OP_SUCCESS) {
            directory_read(&app->directory, app->directory.current_path);
            app_update_git_status(app);

            // Find the new folder and select it
            for (int i = 0; i < app->directory.count; i++) {
                if (strcmp(app->directory.entries[i].name, new_name) == 0) {
                    app->selected_index = i;
                    selection_clear(&app->selection);
                    browser_ensure_visible(app);

                    // Enter rename mode for the new folder
                    app_enter_rename_mode(app);
                    break;
                }
            }
        }
    }

    // Duplicate: Cmd+D
    if (cmd_down && IsKeyPressed(KEY_D)) {
        if (app->directory.count > 0) {
            if (app->selection.count > 0) {
                for (int i = 0; i < app->selection.count; i++) {
                    file_duplicate(app->directory.entries[app->selection.indices[i]].path);
                }
            } else {
                file_duplicate(app->directory.entries[app->selected_index].path);
            }
            directory_read(&app->directory, app->directory.current_path);
            app_update_git_status(app);
        }
    }

    // Navigation
    int prev_selected = app->selected_index;

    // Check if mouse is over preview pane (arrow keys should scroll preview, not navigate)
    bool mouse_over_preview = false;
    if (preview_is_visible(&app->preview)) {
        Vector2 mouse = GetMousePosition();
        int preview_x = app->width - preview_get_width(&app->preview);
        mouse_over_preview = (mouse.x >= preview_x);
    }

    // j or Down - move down (skip if mouse over preview - let preview handle arrows)
    if (IsKeyPressed(KEY_J) || (!mouse_over_preview && (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)))) {
        if (app->selected_index < app->directory.count - 1) {
            app->selected_index++;
            // Shift extends selection
            if (shift_down) {
                if (app->selection.anchor_index < 0) {
                    app->selection.anchor_index = prev_selected;
                }
                selection_range(&app->selection, app->selection.anchor_index, app->selected_index);
            } else if (!cmd_down) {
                selection_clear(&app->selection);
                app->selection.anchor_index = app->selected_index;
            }
        }
    }

    // k or Up - move up (but not Cmd+K which is AI command, skip arrow if mouse over preview)
    if ((!cmd_down && IsKeyPressed(KEY_K)) || (!mouse_over_preview && (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)))) {
        if (app->selected_index > 0) {
            app->selected_index--;
            // Shift extends selection
            if (shift_down) {
                if (app->selection.anchor_index < 0) {
                    app->selection.anchor_index = prev_selected;
                }
                selection_range(&app->selection, app->selection.anchor_index, app->selected_index);
            } else if (!cmd_down) {
                selection_clear(&app->selection);
                app->selection.anchor_index = app->selected_index;
            }
        }
    }

    // l or Right or Enter - enter directory
    if (IsKeyPressed(KEY_L) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_ENTER)) {
        if (app->directory.count > 0) {
            FileEntry *entry = &app->directory.entries[app->selected_index];
            if (entry->is_directory) {
                if (directory_enter(&app->directory, app->selected_index)) {
                    app->selected_index = 0;
                    app->scroll_offset = 0;
                    selection_clear(&app->selection);
                    history_push(&app->history, app->directory.current_path);
                    breadcrumb_update(&app->breadcrumb, app->directory.current_path);
                    app_update_git_status(app);
                }
            }
        }
    }

    // h or Left or Backspace - go to parent (but not Cmd+Backspace which is delete)
    if (IsKeyPressed(KEY_H) || IsKeyPressed(KEY_LEFT) || (!cmd_down && IsKeyPressed(KEY_BACKSPACE))) {
        if (directory_go_parent(&app->directory)) {
            app->selected_index = 0;
            app->scroll_offset = 0;
            selection_clear(&app->selection);
            history_push(&app->history, app->directory.current_path);
            breadcrumb_update(&app->breadcrumb, app->directory.current_path);
            app_update_git_status(app);
        }
    }

    // G - go to last item
    if (IsKeyPressed(KEY_G)) {
        if (shift_down) {
            // Shift+G (capital G) - go to end
            app->selected_index = app->directory.count > 0 ? app->directory.count - 1 : 0;
            selection_clear(&app->selection);
        } else {
            // lowercase g - check for gg
            if (app->g_pressed) {
                // Second g pressed - go to top
                app->selected_index = 0;
                app->g_pressed = false;
                app->g_timer = 0.0f;
                selection_clear(&app->selection);
            } else {
                // First g pressed - start timer
                app->g_pressed = true;
                app->g_timer = 0.0f;
            }
        }
    }

    // Adjust scroll to keep selection visible
    if (app->selected_index != prev_selected) {
        browser_ensure_visible(app);
    }

    // Mouse wheel / trackpad scrolling in file browser (only when not over preview pane)
    if (!mouse_over_preview) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            // Scroll 3 lines per wheel tick
            int scroll_amount = (int)(-wheel * 3);
            app->scroll_offset += scroll_amount;

            // Clamp scroll offset
            if (app->scroll_offset < 0) {
                app->scroll_offset = 0;
            }
            int max_scroll = app->directory.count - app->visible_rows;
            if (max_scroll < 0) max_scroll = 0;
            if (app->scroll_offset > max_scroll) {
                app->scroll_offset = max_scroll;
            }
        }
    }

    // Handle sidebar input
    sidebar_handle_input(app);

    // Handle tabs input
    tabs_handle_input(app);

    // Update preview if selection changed
    if (preview_is_visible(&app->preview) && app->directory.count > 0) {
        FileEntry *entry = &app->directory.entries[app->selected_index];
        if (!entry->is_directory) {
            preview_load(&app->preview, entry->path);
        } else {
            preview_clear(&app->preview);
        }
    }

    // Sync current tab state
    tabs_sync_from_app(&app->tabs, app);
}

void app_update(App *app)
{
    app->fps = GetFPS();

    // Update performance systems
    perf_update(&app->perf, GetFrameTime());

    // Calculate content area dimensions
    int content_width = sidebar_get_content_width(app);
    int content_height = app->height - STATUSBAR_HEIGHT;

    // Calculate visible rows for list view
    int browser_height = content_height - ROW_HEIGHT; // Subtract header
    app->visible_rows = browser_height / ROW_HEIGHT;

    // Calculate grid dimensions
    app->grid_cols = (content_width - PADDING * 2) / GRID_ITEM_WIDTH;
    if (app->grid_cols < 1) app->grid_cols = 1;
    app->grid_rows = (content_height - PADDING * 2) / GRID_ITEM_HEIGHT;
    if (app->grid_rows < 1) app->grid_rows = 1;

    // Handle input
    app_handle_input(app);

    // Handle browser input (rubber band selection in grid view)
    browser_handle_input(app);

    // Update command bar animation/state
    command_bar_update(&app->command_bar, GetFrameTime());

    // Update summary progress indicator animation
    if (atomic_load(&app->summary_thread_active)) {
        progress_indicator_update(&app->summary_progress, GetFrameTime());
    }

    // Update image edit progress indicator animation
    if (app->preview.edit_state == IMAGE_EDIT_LOADING) {
        progress_indicator_update(&app->preview.edit_progress, GetFrameTime());
    }

    // Update text edit progress indicator animation
    if (app->text_edit_state == TEXT_EDIT_LOADING) {
        progress_indicator_update(&app->text_edit_progress, GetFrameTime());
    }

    // Check if command bar operations require directory refresh
    if (command_bar_needs_refresh(&app->command_bar)) {
        directory_read(&app->directory, app->directory.current_path);
        app_update_git_status(app);
        // Also refresh dual pane if active
        if (app->dual_pane.enabled) {
            PaneState *active = dual_pane_get_active_pane(&app->dual_pane);
            directory_read(&active->directory, active->current_path);
        }
    }

    // Handle image edit execution
    if (app->preview.visible && app->preview.type == PREVIEW_IMAGE &&
        app->preview.edit_state == IMAGE_EDIT_LOADING) {

        // Get Gemini client from command bar
        GeminiClient *gemini = app->command_bar.gemini;
        if (!gemini || !gemini_client_is_valid(gemini)) {
            strncpy(app->preview.edit_error, "Gemini API not configured", sizeof(app->preview.edit_error) - 1);
            app->preview.edit_state = IMAGE_EDIT_ERROR;
        } else {
            // Build edit request
            GeminiImageEditRequest req;
            gemini_edit_request_init(&req);
            gemini_edit_request_set_prompt(&req, app->preview.edit_buffer);
            gemini_edit_request_set_source_image(&req, app->preview.file_path);

            GeminiImageResponse resp;
            gemini_response_init(&resp);

            // Execute edit
            bool success = gemini_edit_image(gemini, &req, &resp);

            if (success && resp.result_type == GEMINI_RESULT_SUCCESS && resp.image_data) {
                // Generate output path
                char output_path[4096];
                if (gemini_generate_edited_path(app->preview.file_path, output_path, sizeof(output_path))) {
                    // Save the edited image
                    if (gemini_save_image(&resp, output_path)) {
                        strncpy(app->preview.edit_result_path, output_path, sizeof(app->preview.edit_result_path) - 1);
                        app->preview.edit_state = IMAGE_EDIT_SUCCESS;

                        // Refresh directory to show new file
                        directory_read(&app->directory, app->directory.current_path);
                        app_update_git_status(app);
                    } else {
                        strncpy(app->preview.edit_error, "Failed to save edited image", sizeof(app->preview.edit_error) - 1);
                        app->preview.edit_state = IMAGE_EDIT_ERROR;
                    }
                } else {
                    strncpy(app->preview.edit_error, "Failed to generate output path", sizeof(app->preview.edit_error) - 1);
                    app->preview.edit_state = IMAGE_EDIT_ERROR;
                }
            } else {
                // Copy error message
                if (resp.error[0]) {
                    strncpy(app->preview.edit_error, resp.error, sizeof(app->preview.edit_error) - 1);
                } else {
                    strncpy(app->preview.edit_error, gemini_result_to_string(resp.result_type),
                            sizeof(app->preview.edit_error) - 1);
                }
                app->preview.edit_state = IMAGE_EDIT_ERROR;
            }

            gemini_response_cleanup(&resp);
        }
    }

    // Handle async summary completion (from context menu)
    if (summarize_async_is_complete(&app->async_summary_request)) {
        pthread_mutex_lock(&app->summary_mutex);
        if (app->async_summary_request.from_context_menu &&
            app->async_summary_request.result.status == SUMM_STATUS_OK) {
            // Show summary in dialog
            const char *filename = strrchr(app->async_summary_request.file_path, '/');
            filename = filename ? filename + 1 : app->async_summary_request.file_path;
            char title[128];
            snprintf(title, sizeof(title), "Summary: %s", filename);
            dialog_summary(&app->dialog, title, app->async_summary_request.result.summary);
        } else if (app->async_summary_request.from_context_menu &&
                   app->async_summary_request.result.status != SUMM_STATUS_OK) {
            // Show error dialog
            dialog_error(&app->dialog, "Summary Error",
                        app->async_summary_request.result.error_message);
        }
        // Clear the request
        app->async_summary_request.from_context_menu = false;
        app->async_summary_request.completed = false;
        pthread_mutex_unlock(&app->summary_mutex);

        // Join thread if still active
        if (atomic_load(&app->summary_thread_active)) {
            pthread_join(app->summary_thread, NULL);
            atomic_store(&app->summary_thread_active, false);
        }
    }

    // Handle text edit with Claude
    if (app->text_edit_state == TEXT_EDIT_LOADING) {
        // Get Claude client from command bar
        ClaudeClient *claude = app->command_bar.claude;
        if (!claude || !claude_client_is_valid(claude)) {
            strncpy(app->text_edit_error, "Claude API not configured",
                    sizeof(app->text_edit_error) - 1);
            app->text_edit_state = TEXT_EDIT_ERROR;
        } else {
            // Read source file
            FILE *f = fopen(app->text_edit_path, "r");
            if (!f) {
                strncpy(app->text_edit_error, "Failed to read source file",
                        sizeof(app->text_edit_error) - 1);
                app->text_edit_state = TEXT_EDIT_ERROR;
            } else {
                // Get file size
                fseek(f, 0, SEEK_END);
                long file_size = ftell(f);
                fseek(f, 0, SEEK_SET);

                // Limit to 32KB
                if (file_size > 32768) {
                    fclose(f);
                    strncpy(app->text_edit_error, "File too large (max 32KB)",
                            sizeof(app->text_edit_error) - 1);
                    app->text_edit_state = TEXT_EDIT_ERROR;
                } else {
                    char *content = malloc(file_size + 1);
                    if (!content) {
                        fclose(f);
                        strncpy(app->text_edit_error, "Memory allocation failed",
                                sizeof(app->text_edit_error) - 1);
                        app->text_edit_state = TEXT_EDIT_ERROR;
                    } else {
                        size_t read_bytes = fread(content, 1, file_size, f);
                        fclose(f);
                        content[read_bytes] = '\0';

                        // Build Claude request
                        ClaudeMessageRequest req;
                        claude_request_init(&req);
                        claude_request_set_system_prompt(&req,
                            "You are a text editing assistant. The user will provide a text file "
                            "and an edit instruction. Apply the edit and output ONLY the edited "
                            "text, with no explanations, no code blocks, no markdown formatting. "
                            "Just output the edited content exactly as it should be saved.");

                        // Build user message with file content and instruction
                        char user_msg[CLAUDE_MAX_MESSAGE_LEN];
                        snprintf(user_msg, sizeof(user_msg),
                                "File content:\n```\n%s\n```\n\nEdit instruction: %s",
                                content, app->text_edit_prompt);
                        claude_request_add_user_message(&req, user_msg);
                        free(content);

                        // Send request
                        ClaudeMessageResponse resp;
                        claude_response_init(&resp);
                        bool success = claude_send_message(claude, &req, &resp);

                        if (success && resp.stop_reason == CLAUDE_STOP_END_TURN &&
                            resp.content[0] != '\0') {
                            // Generate output path with _edited_N suffix
                            char output_path[4096];
                            const char *dot = strrchr(app->text_edit_path, '.');
                            const char *slash = strrchr(app->text_edit_path, '/');
                            if (dot && (!slash || dot > slash)) {
                                // Has extension
                                size_t base_len = dot - app->text_edit_path;
                                char base[4096];
                                strncpy(base, app->text_edit_path, base_len);
                                base[base_len] = '\0';

                                // Find unique name
                                int n = 1;
                                do {
                                    snprintf(output_path, sizeof(output_path),
                                            "%s_edited_%d%s", base, n, dot);
                                    n++;
                                } while (access(output_path, F_OK) == 0 && n < 1000);
                            } else {
                                // No extension
                                int n = 1;
                                do {
                                    snprintf(output_path, sizeof(output_path),
                                            "%s_edited_%d", app->text_edit_path, n);
                                    n++;
                                } while (access(output_path, F_OK) == 0 && n < 1000);
                            }

                            // Save edited content
                            FILE *out = fopen(output_path, "w");
                            if (out) {
                                fputs(resp.content, out);
                                fclose(out);
                                strncpy(app->text_edit_result_path, output_path,
                                        sizeof(app->text_edit_result_path) - 1);
                                app->text_edit_state = TEXT_EDIT_SUCCESS;

                                // Refresh directory
                                directory_read(&app->directory, app->directory.current_path);
                                app_update_git_status(app);
                            } else {
                                strncpy(app->text_edit_error, "Failed to save edited file",
                                        sizeof(app->text_edit_error) - 1);
                                app->text_edit_state = TEXT_EDIT_ERROR;
                            }
                        } else {
                            // Error
                            if (resp.error) {
                                strncpy(app->text_edit_error, resp.error,
                                        sizeof(app->text_edit_error) - 1);
                            } else {
                                strncpy(app->text_edit_error, "Failed to edit text",
                                        sizeof(app->text_edit_error) - 1);
                            }
                            app->text_edit_state = TEXT_EDIT_ERROR;
                        }

                        claude_response_cleanup(&resp);
                        claude_request_cleanup(&req);
                    }
                }
            }
        }
    }
}

// Draw text edit overlay UI
static void app_draw_text_edit_overlay(App *app)
{
    if (app->text_edit_state == TEXT_EDIT_NONE) return;

    // Semi-transparent background overlay
    DrawRectangle(0, 0, app->width, app->height, (Color){0, 0, 0, 180});

    // Dialog dimensions
    int dialog_width = 500;
    int dialog_height = 200;
    int dialog_x = (app->width - dialog_width) / 2;
    int dialog_y = (app->height - dialog_height) / 2;

    // Draw dialog background
    DrawRectangle(dialog_x, dialog_y, dialog_width, dialog_height, g_theme.sidebar);
    DrawRectangleLinesEx((Rectangle){dialog_x, dialog_y, dialog_width, dialog_height},
                         2, g_theme.border);

    int font_size = 14;
    Font font = font_get(font_size);
    int padding = 20;

    if (app->text_edit_state == TEXT_EDIT_INPUT) {
        // Title
        const char *title = "Edit Text with AI";
        Vector2 title_size = MeasureTextEx(font, title, font_size + 4, 1);
        DrawTextEx(font, title,
                   (Vector2){dialog_x + (dialog_width - title_size.x) / 2, dialog_y + padding},
                   font_size + 4, 1, g_theme.textPrimary);

        // File name
        const char *filename = strrchr(app->text_edit_path, '/');
        filename = filename ? filename + 1 : app->text_edit_path;
        char file_label[256];
        snprintf(file_label, sizeof(file_label), "File: %s", filename);
        DrawTextEx(font, file_label,
                   (Vector2){dialog_x + padding, dialog_y + padding + 35},
                   font_size, 1, g_theme.textSecondary);

        // Prompt label
        DrawTextEx(font, "Edit instruction:",
                   (Vector2){dialog_x + padding, dialog_y + padding + 60},
                   font_size, 1, g_theme.textPrimary);

        // Input box background
        int input_y = dialog_y + padding + 80;
        int input_width = dialog_width - padding * 2;
        int input_height = 30;
        DrawRectangle(dialog_x + padding, input_y, input_width, input_height, g_theme.background);
        DrawRectangleLinesEx((Rectangle){dialog_x + padding, input_y, input_width, input_height},
                             1, g_theme.aiAccent);

        // Input text
        DrawTextEx(font, app->text_edit_prompt,
                   (Vector2){dialog_x + padding + 5, input_y + 8},
                   font_size, 1, g_theme.textPrimary);

        // Cursor
        float blink = (float)fmod(GetTime() * 2, 2.0);
        if (blink < 1.0f) {
            int cursor_x = dialog_x + padding + 5 +
                          (int)MeasureTextEx(font, app->text_edit_prompt, font_size, 1).x;
            // Adjust for cursor position within string
            if (app->text_edit_cursor < (int)strlen(app->text_edit_prompt)) {
                char tmp[1024];
                strncpy(tmp, app->text_edit_prompt, app->text_edit_cursor);
                tmp[app->text_edit_cursor] = '\0';
                cursor_x = dialog_x + padding + 5 +
                          (int)MeasureTextEx(font, tmp, font_size, 1).x;
            }
            DrawLine(cursor_x, input_y + 5, cursor_x, input_y + input_height - 5, g_theme.textPrimary);
        }

        // Instructions
        DrawTextEx(font, "Enter: Submit | Escape: Cancel",
                   (Vector2){dialog_x + padding, dialog_y + dialog_height - padding - 15},
                   font_size - 2, 1, g_theme.textSecondary);

    } else if (app->text_edit_state == TEXT_EDIT_LOADING) {
        // Loading indicator using shared spinner
        int cx = dialog_x + dialog_width / 2;
        int cy = dialog_y + dialog_height / 2 - 10;
        progress_indicator_draw_spinner(&app->text_edit_progress, cx, cy, 15, g_theme.aiAccent);

        // Draw message below spinner
        Vector2 msg_size = MeasureTextEx(font, app->text_edit_progress.message, font_size, 1);
        DrawTextEx(font, app->text_edit_progress.message,
                   (Vector2){dialog_x + (dialog_width - msg_size.x) / 2, (float)(cy + 25)},
                   font_size, 1, g_theme.textSecondary);

    } else if (app->text_edit_state == TEXT_EDIT_SUCCESS) {
        // Success message
        const char *title = "Edit Complete";
        Vector2 title_size = MeasureTextEx(font, title, font_size + 4, 1);
        DrawTextEx(font, title,
                   (Vector2){dialog_x + (dialog_width - title_size.x) / 2, dialog_y + padding},
                   font_size + 4, 1, g_theme.success);

        // Result filename
        const char *result_name = strrchr(app->text_edit_result_path, '/');
        result_name = result_name ? result_name + 1 : app->text_edit_result_path;
        char result_msg[512];
        snprintf(result_msg, sizeof(result_msg), "Saved as: %s", result_name);
        Vector2 msg_size = MeasureTextEx(font, result_msg, font_size, 1);
        DrawTextEx(font, result_msg,
                   (Vector2){dialog_x + (dialog_width - msg_size.x) / 2,
                            dialog_y + dialog_height / 2 - 10},
                   font_size, 1, g_theme.textPrimary);

        DrawTextEx(font, "Press any key to close",
                   (Vector2){dialog_x + padding, dialog_y + dialog_height - padding - 15},
                   font_size - 2, 1, g_theme.textSecondary);

    } else if (app->text_edit_state == TEXT_EDIT_ERROR) {
        // Error message
        const char *title = "Edit Failed";
        Vector2 title_size = MeasureTextEx(font, title, font_size + 4, 1);
        DrawTextEx(font, title,
                   (Vector2){dialog_x + (dialog_width - title_size.x) / 2, dialog_y + padding},
                   font_size + 4, 1, g_theme.error);

        // Error details
        Vector2 err_size = MeasureTextEx(font, app->text_edit_error, font_size, 1);
        DrawTextEx(font, app->text_edit_error,
                   (Vector2){dialog_x + (dialog_width - err_size.x) / 2,
                            dialog_y + dialog_height / 2 - 10},
                   font_size, 1, g_theme.textPrimary);

        DrawTextEx(font, "Press any key to close",
                   (Vector2){dialog_x + padding, dialog_y + dialog_height - padding - 15},
                   font_size - 2, 1, g_theme.textSecondary);
    }
}

void app_draw(App *app)
{
    BeginDrawing();

    ClearBackground(g_theme.background);

    // Draw sidebar
    sidebar_draw(app);

    // Draw tab bar
    tabs_draw(app);

    // Draw search bar if active
    search_draw(app);

    // Draw breadcrumb bar
    breadcrumb_draw(app);

    // Draw file browser or dual pane (depends on mode)
    if (dual_pane_is_enabled(&app->dual_pane)) {
        dual_pane_draw(app);
    } else {
        browser_draw(app);
        // Draw rubber band selection overlay
        browser_draw_rubber_band(app);
        // Draw preview panel (only in normal mode)
        preview_draw(app);
    }

    // Draw status bar
    statusbar_draw(app);

    // Draw dialog on top of everything (modal)
    dialog_draw(app);

    // Draw file view modal (full content view for text/images)
    file_view_modal_draw(app);

    // Draw text edit overlay (on top of dialog)
    app_draw_text_edit_overlay(app);

    // Draw context menu (on top, after dialog)
    context_menu_draw(app);

    // Draw AI command bar (on top of everything)
    command_bar_draw(&app->command_bar, app->width, app->height);

    // Draw summary progress overlay (when summarizing)
    if (atomic_load(&app->summary_thread_active)) {
        Rectangle full_screen = {0, 0, (float)app->width, (float)app->height};
        progress_indicator_draw_overlay(&app->summary_progress, full_screen, g_theme.aiAccent);
    }

    EndDrawing();
}

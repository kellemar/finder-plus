#ifndef APP_H
#define APP_H

#include "raylib.h"
#include "core/filesystem.h"
#include "core/operations.h"
#include "core/search.h"
#include "core/git.h"
#include "core/operation_queue.h"
#include "ui/tabs.h"
#include "ui/queue_panel.h"
#include "ui/palette.h"
#include "ui/preview.h"
#include "ui/file_view_modal.h"
#include "ui/breadcrumb.h"
#include "ui/dialog.h"
#include "ui/context_menu.h"
#include "ui/dual_pane.h"
#include "ui/command_bar.h"
#include "core/network.h"
#include "utils/theme.h"
#include "utils/keybindings.h"
#include "utils/perf.h"
#include "ai/embeddings.h"
#include "ai/vectordb.h"
#include "ai/clip.h"
#include "ai/indexer.h"
#include "ai/semantic_search.h"
#include "ai/visual_search.h"
#include "ai/summarize.h"
#include "ai/summarize_async.h"
#include "ui/browser.h"
#include "ui/progress_indicator.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

#define APP_NAME "Finder Plus"
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define MAX_SELECTION 4096
#define MAX_HISTORY 64
#define SIDEBAR_DEFAULT_WIDTH 200
#define SIDEBAR_MIN_WIDTH 120
#define SIDEBAR_MAX_WIDTH 400

// View modes
typedef enum ViewMode {
    VIEW_LIST,
    VIEW_GRID,
    VIEW_COLUMN
} ViewMode;

// Text edit state (AI-powered text editing)
typedef enum TextEditState {
    TEXT_EDIT_NONE,        // Not editing
    TEXT_EDIT_INPUT,       // Entering edit instruction
    TEXT_EDIT_LOADING,     // Waiting for Claude API
    TEXT_EDIT_SUCCESS,     // Edit completed
    TEXT_EDIT_ERROR        // Edit failed
} TextEditState;

// Selection state for multi-select
typedef struct SelectionState {
    int *indices;           // Array of selected indices
    int count;              // Number of selected items
    int capacity;           // Capacity of indices array
    int anchor_index;       // Anchor for range selection
} SelectionState;

// Sidebar favorite item
typedef struct SidebarItem {
    char name[64];
    char path[PATH_MAX_LEN];
    bool is_volume;
} SidebarItem;

// Sidebar state
typedef struct SidebarState {
    SidebarItem favorites[16];
    int favorite_count;
    SidebarItem volumes[16];
    int volume_count;
    int width;
    bool collapsed;
    bool resizing;
    int hovered_index;          // -1 if none, 0+ for favorites, 100+ for volumes
} SidebarState;

// Column view state (Miller columns)
#define MAX_COLUMN_DEPTH 16          // Maximum navigation depth
typedef struct ColumnState {
    char paths[MAX_COLUMN_DEPTH][PATH_MAX_LEN];  // Paths for each column
    int selected[MAX_COLUMN_DEPTH];              // Selected index in each column
    int scroll[MAX_COLUMN_DEPTH];                // Vertical scroll offset in each column
    int column_count;                            // Number of active columns (depth)
    int h_scroll;                                // Horizontal scroll (first visible column)
    int visible_columns;                         // Number of columns that fit on screen
    char last_initialized_path[PATH_MAX_LEN];    // Track last path for change detection
} ColumnState;

// Navigation history
typedef struct HistoryState {
    char paths[MAX_HISTORY][PATH_MAX_LEN];
    int count;
    int current;
} HistoryState;

// Application state
typedef struct App {
    // Window
    int width;
    int height;
    bool fullscreen;
    bool should_close;

    // View mode
    ViewMode view_mode;

    // File browser state
    DirectoryState directory;
    int selected_index;          // Primary selected (cursor position)
    int scroll_offset;           // For virtual scrolling
    int visible_rows;            // Number of visible rows

    // Multi-selection
    SelectionState selection;

    // Sidebar
    SidebarState sidebar;

    // Column view (Miller columns)
    ColumnState columns;

    // Navigation history
    HistoryState history;

    // Grid view
    int grid_cols;               // Number of columns in grid
    int grid_rows;               // Visible rows in grid

    // Rubber band selection (grid view)
    bool rubber_band_active;     // Is rubber band selection in progress
    Vector2 rubber_band_start;   // Start point of rubber band (screen coords)
    Vector2 rubber_band_current; // Current point of rubber band (screen coords)

    // Input state
    bool g_pressed;              // For gg command
    float g_timer;               // Timeout for gg command

    // Rename mode
    bool rename_mode;
    char rename_buffer[NAME_MAX_LEN];
    int rename_cursor;
    int rename_index;

    // Clipboard for file operations
    ClipboardState clipboard;

    // Tab management
    TabState tabs;

    // Search state
    SearchState search;

    // Preview state
    PreviewState preview;

    // Breadcrumb state
    BreadcrumbState breadcrumb;

    // Git integration
    GitState git;
    GitStatusResult git_status;
    bool git_enabled;

    // Operation queue
    OperationQueue op_queue;
    QueuePanelState queue_panel;

    // Command palette
    PaletteState palette;

    // Custom keybindings
    KeyBindingConfig keybindings;

    // Dialog system
    DialogState dialog;

    // Context menu (right-click)
    ContextMenuState context_menu;

    // Dual pane mode (Phase 8)
    DualPaneState dual_pane;

    // Network locations (Phase 8)
    NetworkManager network;

    // AI Command bar (Cmd+K)
    CommandBar command_bar;

    // Local AI (Phase 5)
    EmbeddingEngine *embedding_engine;
    VectorDB *vectordb;
    CLIPEngine *clip_engine;
    Indexer *indexer;
    SemanticSearch *semantic_search;
    VisualSearch *visual_search;
    bool ai_enabled;           // Whether AI features are enabled
    bool ai_indexing;          // Whether background indexing is active

    // Performance (Phase 8)
    PerfManager perf;
    float fps;
    bool show_perf_stats;    // Toggle to display detailed performance stats

    // Browser mouse/hover state (Phase 8)
    BrowserState browser_state;

    // Async summary threading (Phase 8)
    pthread_t summary_thread;
    pthread_mutex_t summary_mutex;
    atomic_bool summary_thread_active;  // Thread-safe flag

    // Summary system (Phase 8)
    AsyncSummaryRequest async_summary_request;
    SummarizeConfig summary_config;
    SummaryCache *summary_cache;
    ProgressIndicator summary_progress;  // Progress indicator for summarization

    // AI Text Edit (Phase 8 - context menu)
    TextEditState text_edit_state;
    char text_edit_path[4096];        // Path to file being edited
    char text_edit_prompt[1024];      // User's edit instruction
    int text_edit_cursor;             // Cursor position in prompt
    char text_edit_result_path[4096]; // Path to edited file
    char text_edit_error[256];        // Error message if failed
    ProgressIndicator text_edit_progress;  // Progress indicator for text edit

    // File View Modal (double-click to view full content)
    FileViewModalState file_view_modal;
} App;

// Initialize the application
void app_init(App *app, const char *start_path);

// Free application resources
void app_free(App *app);

// Update application state (called every frame)
void app_update(App *app);

// Render the application (called every frame)
void app_draw(App *app);

// Handle keyboard input
void app_handle_input(App *app);

// Selection functions
void selection_init(SelectionState *sel);
void selection_free(SelectionState *sel);
void selection_clear(SelectionState *sel);
void selection_add(SelectionState *sel, int index);
void selection_remove(SelectionState *sel, int index);
void selection_toggle(SelectionState *sel, int index);
bool selection_contains(SelectionState *sel, int index);
void selection_range(SelectionState *sel, int from, int to);
void selection_select_all(App *app);

// Sidebar functions
void sidebar_init(SidebarState *sidebar);
void sidebar_refresh_volumes(SidebarState *sidebar);

// History functions
void history_init(HistoryState *history);
void history_push(HistoryState *history, const char *path);
const char* history_back(HistoryState *history);
const char* history_forward(HistoryState *history);
bool history_can_go_back(HistoryState *history);
bool history_can_go_forward(HistoryState *history);

// View mode helpers
const char* view_mode_name(ViewMode mode);

#endif // APP_H

#ifndef OPERATIONS_H
#define OPERATIONS_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_CLIPBOARD_ITEMS 128
#define MAX_PATH_LENGTH 1024

// Operation types
typedef enum OperationType {
    OP_NONE,
    OP_COPY,
    OP_CUT
} OperationType;

// File operation result
typedef enum OperationResult {
    OP_SUCCESS,
    OP_ERROR_NOT_FOUND,
    OP_ERROR_PERMISSION,
    OP_ERROR_EXISTS,
    OP_ERROR_INVALID,
    OP_ERROR_DISK_FULL,
    OP_ERROR_UNKNOWN
} OperationResult;

// Clipboard state for file operations
typedef struct ClipboardState {
    char paths[MAX_CLIPBOARD_ITEMS][MAX_PATH_LENGTH];  // Paths of copied/cut items
    int count;                                          // Number of items
    OperationType operation;                            // Copy or cut
} ClipboardState;

// Initialize clipboard state
void clipboard_init(ClipboardState *clipboard);

// Clear clipboard
void clipboard_clear(ClipboardState *clipboard);

// Copy files to clipboard
void clipboard_copy(ClipboardState *clipboard, const char **paths, int count);

// Cut files to clipboard
void clipboard_cut(ClipboardState *clipboard, const char **paths, int count);

// Check if clipboard has items
bool clipboard_has_items(ClipboardState *clipboard);

// Check if path is in clipboard (for visual feedback)
bool clipboard_contains(ClipboardState *clipboard, const char *path);

// Sync clipboard from system pasteboard (for cross-app paste into app)
// Call this before paste to pick up files copied from other apps
void clipboard_sync_from_system(ClipboardState *clipboard);

// Copy file paths as text to system clipboard
bool clipboard_copy_paths_as_text(ClipboardState *clipboard);

// File operation functions

// Copy a file or directory to destination
// Returns operation result
OperationResult file_copy(const char *source, const char *dest_dir);

// Move a file or directory to destination
OperationResult file_move(const char *source, const char *dest_dir);

// Delete a file or directory (move to trash)
OperationResult file_delete(const char *path);

// Rename a file or directory
OperationResult file_rename(const char *path, const char *new_name);

// Create a new directory
OperationResult file_create_directory(const char *parent_dir, const char *name);

// Create a new file with optional content
// If content is NULL or empty, creates an empty file
OperationResult file_create_file(const char *parent_dir, const char *name, const char *content);

// Duplicate a file or directory
OperationResult file_duplicate(const char *path);

// Paste clipboard items to destination directory
// If cut, moves items; if copy, copies items
// Handles name conflicts by auto-renaming
int clipboard_paste(ClipboardState *clipboard, const char *dest_dir);

// Get last error message
const char* operations_get_error(void);

// Generate a unique filename (adds suffix like " (1)", " (2)", etc.)
void generate_unique_name(const char *base_path, char *output, size_t output_size);

#endif // OPERATIONS_H

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <sys/types.h>

#define PATH_MAX_LEN 4096
#define NAME_MAX_LEN 256
#define EXTENSION_MAX_LEN 32

// Git file status (matches git.h GitFileStatus)
typedef enum FileGitStatus {
    FILE_GIT_NONE = 0,
    FILE_GIT_UNTRACKED,
    FILE_GIT_MODIFIED,
    FILE_GIT_STAGED,
    FILE_GIT_DELETED,
    FILE_GIT_RENAMED,
    FILE_GIT_CONFLICT,
    FILE_GIT_IGNORED
} FileGitStatus;

// File entry representing a single file or directory
typedef struct FileEntry {
    char path[PATH_MAX_LEN];        // Full path
    char name[NAME_MAX_LEN];        // File/folder name
    char extension[EXTENSION_MAX_LEN]; // File extension (lowercase, no dot)
    bool is_directory;
    bool is_hidden;
    bool is_symlink;
    off_t size;                     // Size in bytes
    time_t modified;                // Last modified time
    time_t created;                 // Creation time (if available)
    mode_t permissions;
    FileGitStatus git_status;       // Git status for this file
} FileEntry;

// Directory state holding all entries
typedef struct DirectoryState {
    FileEntry *entries;
    int count;
    int capacity;
    char current_path[PATH_MAX_LEN];
    bool show_hidden;
    bool is_loading;
    char error_message[256];
} DirectoryState;

// Sort options for directory entries
typedef enum SortBy {
    SORT_BY_NAME,
    SORT_BY_SIZE,
    SORT_BY_MODIFIED,
    SORT_BY_TYPE
} SortBy;

// Initialize a directory state
void directory_state_init(DirectoryState *state);

// Free resources in directory state
void directory_state_free(DirectoryState *state);

// Read directory contents into state
// Returns true on success, false on error (check state->error_message)
bool directory_read(DirectoryState *state, const char *path);

// Sort entries in directory state
void directory_sort(DirectoryState *state, SortBy sort_by, bool ascending);

// Navigate to parent directory
bool directory_go_parent(DirectoryState *state);

// Navigate into a subdirectory (by index)
bool directory_enter(DirectoryState *state, int index);

// Toggle showing hidden files
void directory_toggle_hidden(DirectoryState *state);

// Get file size as human-readable string (e.g., "4.2 KB")
void format_file_size(off_t size, char *buffer, size_t buffer_size);

// Get modified time as human-readable string (e.g., "Jan 10, 2024")
void format_modified_time(time_t time, char *buffer, size_t buffer_size);

// Get free disk space for a path
off_t get_free_disk_space(const char *path);

// Forward declaration for caching
struct DirCache;

// Read directory with cache support
// Checks cache first, reads from disk if not cached, then stores in cache
bool directory_read_cached(DirectoryState *state, const char *path, struct DirCache *cache);

// Copy directory state (deep copy)
void directory_state_copy(DirectoryState *dest, const DirectoryState *src);

#endif // FILESYSTEM_H

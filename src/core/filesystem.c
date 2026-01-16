#include "filesystem.h"
#include "../utils/perf.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <errno.h>

#define INITIAL_CAPACITY 256

void directory_state_init(DirectoryState *state)
{
    state->entries = NULL;
    state->count = 0;
    state->capacity = 0;
    state->current_path[0] = '\0';
    state->show_hidden = false;
    state->is_loading = false;
    state->error_message[0] = '\0';
}

void directory_state_free(DirectoryState *state)
{
    if (state->entries) {
        free(state->entries);
        state->entries = NULL;
    }
    state->count = 0;
    state->capacity = 0;
}

static bool ensure_capacity(DirectoryState *state, int needed)
{
    if (needed <= state->capacity) {
        return true;
    }

    int new_capacity = state->capacity == 0 ? INITIAL_CAPACITY : state->capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    FileEntry *new_entries = realloc(state->entries, new_capacity * sizeof(FileEntry));
    if (!new_entries) {
        return false;
    }

    state->entries = new_entries;
    state->capacity = new_capacity;
    return true;
}

static void extract_extension(const char *name, char *extension, size_t ext_size)
{
    extension[0] = '\0';

    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) {
        return;
    }

    // Skip the dot and copy extension in lowercase
    const char *src = dot + 1;
    size_t i = 0;
    while (*src && i < ext_size - 1) {
        char c = *src;
        if (c >= 'A' && c <= 'Z') {
            c = c + ('a' - 'A');
        }
        extension[i] = c;
        i++;
        src++;
    }
    extension[i] = '\0';
}

bool directory_read(DirectoryState *state, const char *path)
{
    state->is_loading = true;
    state->error_message[0] = '\0';

    // Resolve path and handle special cases
    char resolved_path[PATH_MAX_LEN];
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(resolved_path, sizeof(resolved_path), "%s%s", home, path + 1);
        } else {
            strncpy(resolved_path, path, sizeof(resolved_path) - 1);
            resolved_path[sizeof(resolved_path) - 1] = '\0';
        }
    } else {
        if (realpath(path, resolved_path) == NULL) {
            snprintf(state->error_message, sizeof(state->error_message),
                     "Cannot resolve path: %s", strerror(errno));
            state->is_loading = false;
            return false;
        }
    }

    DIR *dir = opendir(resolved_path);
    if (!dir) {
        snprintf(state->error_message, sizeof(state->error_message),
                 "Cannot open directory: %s", strerror(errno));
        state->is_loading = false;
        return false;
    }

    // Clear existing entries
    state->count = 0;

    strncpy(state->current_path, resolved_path, sizeof(state->current_path) - 1);
    state->current_path[sizeof(state->current_path) - 1] = '\0';

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        bool is_hidden = entry->d_name[0] == '.';
        if (is_hidden && !state->show_hidden) {
            continue;
        }

        if (!ensure_capacity(state, state->count + 1)) {
            closedir(dir);
            snprintf(state->error_message, sizeof(state->error_message),
                     "Out of memory");
            state->is_loading = false;
            return false;
        }

        FileEntry *fe = &state->entries[state->count];

        // Build full path
        snprintf(fe->path, sizeof(fe->path), "%s/%s", resolved_path, entry->d_name);

        // Copy name
        strncpy(fe->name, entry->d_name, sizeof(fe->name) - 1);
        fe->name[sizeof(fe->name) - 1] = '\0';

        fe->is_hidden = is_hidden;

        // Get file info with stat
        struct stat st;
        if (lstat(fe->path, &st) == 0) {
            fe->is_symlink = S_ISLNK(st.st_mode);

            // For symlinks, stat the target
            if (fe->is_symlink) {
                struct stat target_st;
                if (stat(fe->path, &target_st) == 0) {
                    fe->is_directory = S_ISDIR(target_st.st_mode);
                    fe->size = target_st.st_size;
                } else {
                    // Broken symlink
                    fe->is_directory = false;
                    fe->size = 0;
                }
            } else {
                fe->is_directory = S_ISDIR(st.st_mode);
                fe->size = st.st_size;
            }

            fe->modified = st.st_mtime;
            fe->created = st.st_birthtime;
            fe->permissions = st.st_mode;
        } else {
            // Stat failed, use defaults
            fe->is_directory = (entry->d_type == DT_DIR);
            fe->is_symlink = (entry->d_type == DT_LNK);
            fe->size = 0;
            fe->modified = 0;
            fe->created = 0;
            fe->permissions = 0;
        }

        // Extract extension for files
        if (!fe->is_directory) {
            extract_extension(fe->name, fe->extension, sizeof(fe->extension));
        } else {
            fe->extension[0] = '\0';
        }

        state->count++;
    }

    closedir(dir);

    // Sort by default (folders first, then alphabetically)
    directory_sort(state, SORT_BY_NAME, true);

    state->is_loading = false;
    return true;
}

// Sort context (using globals since qsort doesn't have context parameter)
static SortBy g_sort_by = SORT_BY_NAME;
static bool g_sort_ascending = true;

static int compare_entries_qsort(const void *a, const void *b)
{
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;

    // Directories always come first
    if (fa->is_directory && !fb->is_directory) {
        return -1;
    }
    if (!fa->is_directory && fb->is_directory) {
        return 1;
    }

    int result = 0;

    switch (g_sort_by) {
        case SORT_BY_NAME:
            result = strcasecmp(fa->name, fb->name);
            break;
        case SORT_BY_SIZE:
            if (fa->size < fb->size) result = -1;
            else if (fa->size > fb->size) result = 1;
            else result = 0;
            break;
        case SORT_BY_MODIFIED:
            if (fa->modified < fb->modified) result = -1;
            else if (fa->modified > fb->modified) result = 1;
            else result = 0;
            break;
        case SORT_BY_TYPE:
            result = strcasecmp(fa->extension, fb->extension);
            if (result == 0) {
                result = strcasecmp(fa->name, fb->name);
            }
            break;
    }

    return g_sort_ascending ? result : -result;
}

static void sort_entries_internal(FileEntry *entries, int count, SortBy sort_by, bool ascending)
{
    if (count <= 1) return;

    // Set global sort parameters
    g_sort_by = sort_by;
    g_sort_ascending = ascending;

    // Use standard qsort for O(n log n) performance
    qsort(entries, count, sizeof(FileEntry), compare_entries_qsort);
}

void directory_sort(DirectoryState *state, SortBy sort_by, bool ascending)
{
    if (state->count <= 1) {
        return;
    }

    sort_entries_internal(state->entries, state->count, sort_by, ascending);
}

bool directory_go_parent(DirectoryState *state)
{
    if (strcmp(state->current_path, "/") == 0) {
        return false; // Already at root
    }

    char parent_path[PATH_MAX_LEN];
    strncpy(parent_path, state->current_path, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';

    char *last_slash = strrchr(parent_path, '/');
    if (last_slash == parent_path) {
        // Parent is root
        parent_path[1] = '\0';
    } else if (last_slash) {
        *last_slash = '\0';
    }

    return directory_read(state, parent_path);
}

bool directory_enter(DirectoryState *state, int index)
{
    if (index < 0 || index >= state->count) {
        return false;
    }

    FileEntry *entry = &state->entries[index];
    if (!entry->is_directory) {
        return false;
    }

    return directory_read(state, entry->path);
}

void directory_toggle_hidden(DirectoryState *state)
{
    state->show_hidden = !state->show_hidden;
    // Re-read current directory
    if (state->current_path[0] != '\0') {
        directory_read(state, state->current_path);
    }
}

void format_file_size(off_t size, char *buffer, size_t buffer_size)
{
    if (size < 1024) {
        snprintf(buffer, buffer_size, "%lld B", (long long)size);
    } else if (size < 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f KB", size / 1024.0);
    } else if (size < 1024 * 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f MB", size / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, buffer_size, "%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
    }
}

void format_modified_time(time_t time_val, char *buffer, size_t buffer_size)
{
    if (time_val == 0) {
        strncpy(buffer, "--", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    struct tm *tm = localtime(&time_val);
    if (tm) {
        strftime(buffer, buffer_size, "%b %d, %Y", tm);
    } else {
        strncpy(buffer, "--", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
    }
}

off_t get_free_disk_space(const char *path)
{
    struct statvfs stat;
    if (statvfs(path, &stat) != 0) {
        return -1;
    }
    return (off_t)stat.f_bavail * stat.f_frsize;
}

void directory_state_copy(DirectoryState *dest, const DirectoryState *src)
{
    directory_state_init(dest);
    strncpy(dest->current_path, src->current_path, PATH_MAX_LEN - 1);
    dest->current_path[PATH_MAX_LEN - 1] = '\0';
    dest->show_hidden = src->show_hidden;
    dest->is_loading = false;

    if (src->count > 0 && src->entries) {
        if (ensure_capacity(dest, src->count)) {
            memcpy(dest->entries, src->entries, src->count * sizeof(FileEntry));
            dest->count = src->count;
        }
    }
}

bool directory_read_cached(DirectoryState *state, const char *path, struct DirCache *cache)
{
    // If no cache provided, just read normally
    if (!cache) {
        return directory_read(state, path);
    }

    // Try to get from cache
    DirectoryState *cached = dir_cache_get(cache, path);
    if (cached) {
        // Copy cached result to state
        directory_state_free(state);
        directory_state_copy(state, cached);
        return true;
    }

    // Not in cache, read from disk
    bool result = directory_read(state, path);

    // Store in cache if successful
    if (result) {
        dir_cache_put(cache, path, state);
    }

    return result;
}

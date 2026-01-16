#include "operations.h"
#include "filesystem.h"
#include "../platform/clipboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>
#include <copyfile.h>

// Error message buffer
static char g_error_message[512] = {0};

void clipboard_init(ClipboardState *clipboard)
{
    clipboard->count = 0;
    clipboard->operation = OP_NONE;
}

void clipboard_clear(ClipboardState *clipboard)
{
    clipboard->count = 0;
    clipboard->operation = OP_NONE;
}

// Helper to set clipboard with paths and sync to system
static void clipboard_set(ClipboardState *clipboard, const char **paths, int count, OperationType op)
{
    clipboard_clear(clipboard);
    clipboard->operation = op;

    for (int i = 0; i < count && i < MAX_CLIPBOARD_ITEMS; i++) {
        strncpy(clipboard->paths[i], paths[i], MAX_PATH_LENGTH - 1);
        clipboard->paths[i][MAX_PATH_LENGTH - 1] = '\0';
        clipboard->count++;
    }

    // Sync to system clipboard for cross-app paste
    platform_clipboard_copy_files(paths, count);
}

void clipboard_copy(ClipboardState *clipboard, const char **paths, int count)
{
    clipboard_set(clipboard, paths, count, OP_COPY);
}

void clipboard_cut(ClipboardState *clipboard, const char **paths, int count)
{
    clipboard_set(clipboard, paths, count, OP_CUT);
}

bool clipboard_has_items(ClipboardState *clipboard)
{
    return clipboard->count > 0 && clipboard->operation != OP_NONE;
}

bool clipboard_contains(ClipboardState *clipboard, const char *path)
{
    for (int i = 0; i < clipboard->count; i++) {
        if (strcmp(clipboard->paths[i], path) == 0) {
            return true;
        }
    }
    return false;
}

void clipboard_sync_from_system(ClipboardState *clipboard)
{
    // Check if system clipboard has files
    if (!platform_clipboard_has_files()) {
        return;
    }

    // Get count from system clipboard
    int sys_count = platform_clipboard_get_file_count();
    if (sys_count <= 0) {
        return;
    }

    // Clear internal clipboard and import from system
    clipboard_clear(clipboard);
    clipboard->operation = OP_COPY; // Treat as copy (we don't know if source was cut)

    for (int i = 0; i < sys_count && i < MAX_CLIPBOARD_ITEMS; i++) {
        const char *path = platform_clipboard_get_file_path(i);
        if (path) {
            strncpy(clipboard->paths[i], path, MAX_PATH_LENGTH - 1);
            clipboard->paths[i][MAX_PATH_LENGTH - 1] = '\0';
            clipboard->count++;
        }
    }
}

bool clipboard_copy_paths_as_text(ClipboardState *clipboard)
{
    if (clipboard->count == 0) {
        return false;
    }

    // Calculate required size (paths + newlines + null)
    size_t total_size = 0;
    for (int i = 0; i < clipboard->count; i++) {
        total_size += strlen(clipboard->paths[i]) + 1;  // +1 for newline
    }

    char *text_buffer = malloc(total_size + 1);
    if (!text_buffer) return false;

    char *ptr = text_buffer;
    for (int i = 0; i < clipboard->count; i++) {
        if (i > 0) *ptr++ = '\n';
        size_t len = strlen(clipboard->paths[i]);
        memcpy(ptr, clipboard->paths[i], len);
        ptr += len;
    }
    *ptr = '\0';

    bool result = platform_clipboard_copy_text(text_buffer);
    free(text_buffer);
    return result;
}

const char* operations_get_error(void)
{
    return g_error_message;
}

// Helper to check if path exists
static bool path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

// Helper to check if path is directory
static bool is_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

// Helper to get basename from path
static const char* get_basename(const char *path)
{
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

// Generate unique name for destination
void generate_unique_name(const char *base_path, char *output, size_t output_size)
{
    if (!path_exists(base_path)) {
        strncpy(output, base_path, output_size - 1);
        output[output_size - 1] = '\0';
        return;
    }

    // Find extension if any
    const char *basename_part = get_basename(base_path);
    const char *dot = strrchr(basename_part, '.');
    size_t base_len = strlen(base_path);
    size_t ext_len = 0;
    char ext[64] = {0};

    if (dot && dot != basename_part) {
        ext_len = strlen(dot);
        strncpy(ext, dot, sizeof(ext) - 1);
        base_len = dot - base_path;
    }

    char base_without_ext[4096];
    strncpy(base_without_ext, base_path, base_len);
    base_without_ext[base_len] = '\0';

    // Try adding suffixes until we find a unique name
    for (int i = 1; i < 10000; i++) {
        snprintf(output, output_size, "%s (%d)%s", base_without_ext, i, ext);
        if (!path_exists(output)) {
            return;
        }
    }

    // Fallback - this shouldn't happen
    strncpy(output, base_path, output_size - 1);
    output[output_size - 1] = '\0';
}

// Validate parent dir and name, then build unique path
static OperationResult validate_and_build_path(const char *parent_dir, const char *name,
                                                const char *type_name,
                                                char *unique_path, size_t path_size)
{
    if (!is_directory(parent_dir)) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Parent is not a directory: %s", parent_dir);
        return OP_ERROR_INVALID;
    }

    if (!name || name[0] == '\0' || strchr(name, '/') != NULL) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Invalid %s name", type_name);
        return OP_ERROR_INVALID;
    }

    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", parent_dir, name);
    generate_unique_name(path, unique_path, path_size);

    return OP_SUCCESS;
}

// Recursive copy using macOS copyfile
static bool copy_recursive(const char *source, const char *dest)
{
    copyfile_state_t state = copyfile_state_alloc();
    int result = copyfile(source, dest, state, COPYFILE_ALL | COPYFILE_RECURSIVE);
    copyfile_state_free(state);

    if (result != 0) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Copy failed: %s", strerror(errno));
        return false;
    }
    return true;
}

// Recursive remove
static bool remove_recursive(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Cannot stat: %s", strerror(errno));
        return false;
    }

    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            snprintf(g_error_message, sizeof(g_error_message),
                     "Cannot open directory: %s", strerror(errno));
            return false;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char child_path[4096];
            snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);

            if (!remove_recursive(child_path)) {
                closedir(dir);
                return false;
            }
        }
        closedir(dir);

        if (rmdir(path) != 0) {
            snprintf(g_error_message, sizeof(g_error_message),
                     "Cannot remove directory: %s", strerror(errno));
            return false;
        }
    } else {
        if (unlink(path) != 0) {
            snprintf(g_error_message, sizeof(g_error_message),
                     "Cannot remove file: %s", strerror(errno));
            return false;
        }
    }

    return true;
}

OperationResult file_copy(const char *source, const char *dest_dir)
{
    g_error_message[0] = '\0';

    if (!path_exists(source)) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Source does not exist: %s", source);
        return OP_ERROR_NOT_FOUND;
    }

    if (!is_directory(dest_dir)) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Destination is not a directory: %s", dest_dir);
        return OP_ERROR_INVALID;
    }

    const char *name = get_basename(source);
    char dest_path[4096];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, name);

    // Generate unique name if destination exists
    char unique_dest[4096];
    generate_unique_name(dest_path, unique_dest, sizeof(unique_dest));

    if (!copy_recursive(source, unique_dest)) {
        return OP_ERROR_UNKNOWN;
    }

    return OP_SUCCESS;
}

OperationResult file_move(const char *source, const char *dest_dir)
{
    g_error_message[0] = '\0';

    if (!path_exists(source)) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Source does not exist: %s", source);
        return OP_ERROR_NOT_FOUND;
    }

    if (!is_directory(dest_dir)) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Destination is not a directory: %s", dest_dir);
        return OP_ERROR_INVALID;
    }

    const char *name = get_basename(source);
    char dest_path[4096];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, name);

    // Generate unique name if destination exists
    char unique_dest[4096];
    generate_unique_name(dest_path, unique_dest, sizeof(unique_dest));

    // Try rename first (fast for same filesystem)
    if (rename(source, unique_dest) == 0) {
        return OP_SUCCESS;
    }

    // If rename fails (cross-device), copy and delete
    if (errno == EXDEV) {
        if (!copy_recursive(source, unique_dest)) {
            return OP_ERROR_UNKNOWN;
        }
        if (!remove_recursive(source)) {
            return OP_ERROR_UNKNOWN;
        }
        return OP_SUCCESS;
    }

    snprintf(g_error_message, sizeof(g_error_message),
             "Move failed: %s", strerror(errno));
    return OP_ERROR_UNKNOWN;
}

OperationResult file_delete(const char *path)
{
    g_error_message[0] = '\0';

    if (!path_exists(path)) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "File does not exist: %s", path);
        return OP_ERROR_NOT_FOUND;
    }

    // Move to Trash on macOS
    // We use a system command for reliable Trash handling
    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
             "osascript -e 'tell application \"Finder\" to delete POSIX file \"%s\"' 2>/dev/null",
             path);

    int result = system(cmd);
    if (result != 0) {
        // Fallback: permanently delete if Trash move fails
        snprintf(g_error_message, sizeof(g_error_message),
                 "Move to Trash failed, file not deleted");
        return OP_ERROR_UNKNOWN;
    }

    return OP_SUCCESS;
}

OperationResult file_rename(const char *path, const char *new_name)
{
    g_error_message[0] = '\0';

    if (!path_exists(path)) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "File does not exist: %s", path);
        return OP_ERROR_NOT_FOUND;
    }

    // Validate new name
    if (!new_name || new_name[0] == '\0' || strchr(new_name, '/') != NULL) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Invalid filename: %s", new_name ? new_name : "(null)");
        return OP_ERROR_INVALID;
    }

    // Build new path
    char dir[4096];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        strcpy(dir, ".");
    }

    char new_path[4096];
    snprintf(new_path, sizeof(new_path), "%s/%s", dir, new_name);

    // Check if target already exists
    if (path_exists(new_path)) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "A file with that name already exists");
        return OP_ERROR_EXISTS;
    }

    if (rename(path, new_path) != 0) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Rename failed: %s", strerror(errno));
        return OP_ERROR_UNKNOWN;
    }

    return OP_SUCCESS;
}

OperationResult file_create_directory(const char *parent_dir, const char *name)
{
    g_error_message[0] = '\0';

    char unique_path[4096];
    OperationResult result = validate_and_build_path(parent_dir, name, "directory",
                                                      unique_path, sizeof(unique_path));
    if (result != OP_SUCCESS) return result;

    if (mkdir(unique_path, 0755) != 0) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Failed to create directory: %s", strerror(errno));
        return OP_ERROR_UNKNOWN;
    }

    return OP_SUCCESS;
}

OperationResult file_create_file(const char *parent_dir, const char *name, const char *content)
{
    g_error_message[0] = '\0';

    char unique_path[4096];
    OperationResult result = validate_and_build_path(parent_dir, name, "file",
                                                      unique_path, sizeof(unique_path));
    if (result != OP_SUCCESS) return result;

    FILE *f = fopen(unique_path, "w");
    if (!f) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "Failed to create file: %s", strerror(errno));
        return OP_ERROR_UNKNOWN;
    }

    // Write content if provided
    if (content && content[0] != '\0') {
        size_t content_len = strlen(content);
        if (fwrite(content, 1, content_len, f) != content_len) {
            snprintf(g_error_message, sizeof(g_error_message),
                     "Failed to write content: %s", strerror(errno));
            fclose(f);
            return OP_ERROR_UNKNOWN;
        }
    }

    fclose(f);
    return OP_SUCCESS;
}

OperationResult file_duplicate(const char *path)
{
    g_error_message[0] = '\0';

    if (!path_exists(path)) {
        snprintf(g_error_message, sizeof(g_error_message),
                 "File does not exist: %s", path);
        return OP_ERROR_NOT_FOUND;
    }

    // Get parent directory
    char parent[4096];
    strncpy(parent, path, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';

    char *last_slash = strrchr(parent, '/');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        strcpy(parent, ".");
    }

    // Generate unique destination name
    char dest[4096];
    generate_unique_name(path, dest, sizeof(dest));

    if (!copy_recursive(path, dest)) {
        return OP_ERROR_UNKNOWN;
    }

    return OP_SUCCESS;
}

int clipboard_paste(ClipboardState *clipboard, const char *dest_dir)
{
    if (!clipboard_has_items(clipboard)) {
        return 0;
    }

    int success_count = 0;

    for (int i = 0; i < clipboard->count; i++) {
        OperationResult result;

        if (clipboard->operation == OP_CUT) {
            result = file_move(clipboard->paths[i], dest_dir);
        } else {
            result = file_copy(clipboard->paths[i], dest_dir);
        }

        if (result == OP_SUCCESS) {
            success_count++;
        }
    }

    // Clear clipboard after cut operation
    if (clipboard->operation == OP_CUT) {
        clipboard_clear(clipboard);
    }

    return success_count;
}

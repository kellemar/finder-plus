#include "tool_executor.h"
#include "../core/operations.h"
#include "../core/filesystem.h"
#include "../ai/semantic_search.h"
#include "../ai/visual_search.h"
#include "../api/gemini_client.h"
#include "../../external/cJSON/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <unistd.h>

// Debug logging for tool executor (set to 1 or use -DTOOL_DEBUG=1)
#ifndef TOOL_DEBUG
#define TOOL_DEBUG 0
#endif

#if TOOL_DEBUG
#define TOOL_LOG(fmt, ...) fprintf(stderr, "[TOOL] " fmt "\n", ##__VA_ARGS__)
#else
#define TOOL_LOG(fmt, ...) ((void)0)
#endif

ToolExecutor *tool_executor_create(ToolRegistry *registry)
{
    ToolExecutor *executor = (ToolExecutor *)calloc(1, sizeof(ToolExecutor));
    if (!executor) return NULL;

    executor->registry = registry;
    getcwd(executor->current_dir, sizeof(executor->current_dir));

    return executor;
}

void tool_executor_destroy(ToolExecutor *executor)
{
    if (executor) {
        free(executor);
    }
}

void tool_executor_set_cwd(ToolExecutor *executor, const char *path)
{
    if (!executor || !path) return;
    strncpy(executor->current_dir, path, sizeof(executor->current_dir) - 1);
    executor->current_dir[sizeof(executor->current_dir) - 1] = '\0';
}

void tool_executor_set_semantic_search(ToolExecutor *executor, SemanticSearch *search)
{
    if (!executor) return;
    executor->semantic_search = search;
}

void tool_executor_set_visual_search(ToolExecutor *executor, VisualSearch *search)
{
    if (!executor) return;
    executor->visual_search = search;
}

void tool_executor_set_gemini_client(ToolExecutor *executor, GeminiClient *client)
{
    if (!executor) return;
    executor->gemini_client = client;
}

// Execute file_list tool
static ToolResult execute_file_list(ToolExecutor *executor __attribute__((unused)), cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    cJSON *path_item = cJSON_GetObjectItem(input, "path");
    if (!path_item || !cJSON_IsString(path_item)) {
        tool_result_set_error(&result, "Missing or invalid 'path' parameter");
        return result;
    }

    const char *path = path_item->valuestring;
    cJSON *show_hidden = cJSON_GetObjectItem(input, "show_hidden");
    bool hidden = show_hidden && cJSON_IsTrue(show_hidden);

    DirectoryState dir;
    directory_state_init(&dir);
    dir.show_hidden = hidden;

    if (!directory_read(&dir, path)) {
        tool_result_set_error(&result, dir.error_message[0] ? dir.error_message : "Failed to read directory");
        directory_state_free(&dir);
        return result;
    }

    // Build JSON output
    cJSON *output = cJSON_CreateObject();
    cJSON *files = cJSON_CreateArray();

    for (int i = 0; i < dir.count; i++) {
        cJSON *file = cJSON_CreateObject();
        cJSON_AddStringToObject(file, "name", dir.entries[i].name);
        cJSON_AddBoolToObject(file, "is_directory", dir.entries[i].is_directory);
        cJSON_AddNumberToObject(file, "size", (double)dir.entries[i].size);
        cJSON_AddBoolToObject(file, "is_hidden", dir.entries[i].is_hidden);

        char date_str[32];
        strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", localtime(&dir.entries[i].modified));
        cJSON_AddStringToObject(file, "modified", date_str);

        cJSON_AddItemToArray(files, file);
    }

    cJSON_AddItemToObject(output, "files", files);
    cJSON_AddNumberToObject(output, "count", dir.count);
    cJSON_AddStringToObject(output, "path", path);

    char *json_str = cJSON_Print(output);
    cJSON_Delete(output);

    tool_result_set_success(&result, json_str, dir.count);
    free(json_str);

    directory_state_free(&dir);
    return result;
}

// Execute file_move tool
static ToolResult execute_file_move(cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    cJSON *source = cJSON_GetObjectItem(input, "source");
    cJSON *destination = cJSON_GetObjectItem(input, "destination");

    if (!source || !cJSON_IsString(source)) {
        tool_result_set_error(&result, "Missing or invalid 'source' parameter");
        return result;
    }
    if (!destination || !cJSON_IsString(destination)) {
        tool_result_set_error(&result, "Missing or invalid 'destination' parameter");
        return result;
    }

    OperationResult op_result = file_move(source->valuestring, destination->valuestring);

    if (op_result == OP_SUCCESS) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Moved '%s' to '%s'", source->valuestring, destination->valuestring);
        tool_result_set_success(&result, msg, 1);
    } else {
        tool_result_set_error(&result, operations_get_error());
    }

    return result;
}

// Execute file_copy tool
static ToolResult execute_file_copy(cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    cJSON *source = cJSON_GetObjectItem(input, "source");
    cJSON *destination = cJSON_GetObjectItem(input, "destination");

    if (!source || !cJSON_IsString(source)) {
        tool_result_set_error(&result, "Missing or invalid 'source' parameter");
        return result;
    }
    if (!destination || !cJSON_IsString(destination)) {
        tool_result_set_error(&result, "Missing or invalid 'destination' parameter");
        return result;
    }

    OperationResult op_result = file_copy(source->valuestring, destination->valuestring);

    if (op_result == OP_SUCCESS) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Copied '%s' to '%s'", source->valuestring, destination->valuestring);
        tool_result_set_success(&result, msg, 1);
    } else {
        tool_result_set_error(&result, operations_get_error());
    }

    return result;
}

// Execute file_delete tool
static ToolResult execute_file_delete(cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    cJSON *paths = cJSON_GetObjectItem(input, "paths");
    if (!paths) {
        tool_result_set_error(&result, "Missing 'paths' parameter");
        return result;
    }

    int deleted_count = 0;
    char msg[1024] = "";

    if (cJSON_IsArray(paths)) {
        int count = cJSON_GetArraySize(paths);
        for (int i = 0; i < count; i++) {
            cJSON *path_item = cJSON_GetArrayItem(paths, i);
            if (path_item && cJSON_IsString(path_item)) {
                if (file_delete(path_item->valuestring) == OP_SUCCESS) {
                    deleted_count++;
                }
            }
        }
        snprintf(msg, sizeof(msg), "Moved %d item(s) to Trash", deleted_count);
    } else if (cJSON_IsString(paths)) {
        if (file_delete(paths->valuestring) == OP_SUCCESS) {
            deleted_count = 1;
            snprintf(msg, sizeof(msg), "Moved '%s' to Trash", paths->valuestring);
        }
    }

    if (deleted_count > 0) {
        tool_result_set_success(&result, msg, deleted_count);
    } else {
        tool_result_set_error(&result, operations_get_error());
    }

    return result;
}

// Execute file_create tool
static ToolResult execute_file_create(cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    cJSON *path = cJSON_GetObjectItem(input, "path");
    if (!path || !cJSON_IsString(path)) {
        tool_result_set_error(&result, "Missing or invalid 'path' parameter");
        return result;
    }

    cJSON *is_dir = cJSON_GetObjectItem(input, "is_directory");
    bool create_dir = is_dir && cJSON_IsTrue(is_dir);

    cJSON *content = cJSON_GetObjectItem(input, "content");
    const char *content_str = (content && cJSON_IsString(content)) ? content->valuestring : NULL;

    // Extract parent directory and name
    char parent[1024];
    char name[256];
    const char *full_path = path->valuestring;

    const char *last_slash = strrchr(full_path, '/');
    if (last_slash) {
        size_t parent_len = (size_t)(last_slash - full_path);
        strncpy(parent, full_path, parent_len);
        parent[parent_len] = '\0';
        strncpy(name, last_slash + 1, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    } else {
        strcpy(parent, ".");
        strncpy(name, full_path, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    OperationResult op_result;
    if (create_dir) {
        op_result = file_create_directory(parent, name);
    } else {
        op_result = file_create_file(parent, name, content_str);
    }

    if (op_result == OP_SUCCESS) {
        char msg[512];
        if (create_dir) {
            snprintf(msg, sizeof(msg), "Created directory '%s'", name);
        } else if (content_str && content_str[0] != '\0') {
            snprintf(msg, sizeof(msg), "Created file '%s' with content (%zu bytes)", name, strlen(content_str));
        } else {
            snprintf(msg, sizeof(msg), "Created file '%s'", name);
        }
        tool_result_set_success(&result, msg, 1);
    } else {
        tool_result_set_error(&result, operations_get_error());
    }

    return result;
}

// Execute file_rename tool
static ToolResult execute_file_rename(cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    cJSON *path = cJSON_GetObjectItem(input, "path");
    cJSON *new_name = cJSON_GetObjectItem(input, "new_name");

    if (!path || !cJSON_IsString(path)) {
        tool_result_set_error(&result, "Missing or invalid 'path' parameter");
        return result;
    }
    if (!new_name || !cJSON_IsString(new_name)) {
        tool_result_set_error(&result, "Missing or invalid 'new_name' parameter");
        return result;
    }

    OperationResult op_result = file_rename(path->valuestring, new_name->valuestring);

    if (op_result == OP_SUCCESS) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Renamed to '%s'", new_name->valuestring);
        tool_result_set_success(&result, msg, 1);
    } else {
        tool_result_set_error(&result, operations_get_error());
    }

    return result;
}

// Helper struct for recursive search
typedef struct {
    const char *pattern;
    cJSON *matches;
    int count;
    int max_results;  // Limit results to avoid memory issues
} SearchContext;

// Helper for recursive directory search
static void search_directory_recursive(const char *dir_path, SearchContext *ctx, int depth)
{
    // Limit depth to prevent stack overflow
    if (depth > 32 || ctx->count >= ctx->max_results) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && ctx->count < ctx->max_results) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        // Check if filename matches pattern
        if (fnmatch(ctx->pattern, entry->d_name, 0) == 0) {
            cJSON_AddItemToArray(ctx->matches, cJSON_CreateString(full_path));
            ctx->count++;
        }

        // Recurse into subdirectories
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            search_directory_recursive(full_path, ctx, depth + 1);
        }
    }
    closedir(dir);
}

// Execute file_search tool
static ToolResult execute_file_search(ToolExecutor *executor __attribute__((unused)), cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    cJSON *path = cJSON_GetObjectItem(input, "path");
    cJSON *pattern = cJSON_GetObjectItem(input, "pattern");

    if (!path || !cJSON_IsString(path)) {
        tool_result_set_error(&result, "Missing or invalid 'path' parameter");
        return result;
    }
    if (!pattern || !cJSON_IsString(pattern)) {
        tool_result_set_error(&result, "Missing or invalid 'pattern' parameter");
        return result;
    }

    cJSON *recursive_item = cJSON_GetObjectItem(input, "recursive");
    bool do_recursive = recursive_item && cJSON_IsTrue(recursive_item);

    cJSON *output = cJSON_CreateObject();
    cJSON *matches = cJSON_CreateArray();

    SearchContext ctx = {
        .pattern = pattern->valuestring,
        .matches = matches,
        .count = 0,
        .max_results = 1000  // Prevent runaway searches
    };

    if (do_recursive) {
        search_directory_recursive(path->valuestring, &ctx, 0);
    } else {
        // Non-recursive: search only in specified directory
        DIR *dir = opendir(path->valuestring);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL && ctx.count < ctx.max_results) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }

                if (fnmatch(pattern->valuestring, entry->d_name, 0) == 0) {
                    char full_path[1024];
                    snprintf(full_path, sizeof(full_path), "%s/%s", path->valuestring, entry->d_name);
                    cJSON_AddItemToArray(matches, cJSON_CreateString(full_path));
                    ctx.count++;
                }
            }
            closedir(dir);
        }
    }

    cJSON_AddItemToObject(output, "matches", matches);
    cJSON_AddNumberToObject(output, "count", ctx.count);
    cJSON_AddStringToObject(output, "pattern", pattern->valuestring);
    cJSON_AddBoolToObject(output, "recursive", do_recursive);

    char *json_str = cJSON_Print(output);
    cJSON_Delete(output);

    tool_result_set_success(&result, json_str, ctx.count);
    free(json_str);

    return result;
}

// Execute file_metadata tool
static ToolResult execute_file_metadata(cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    cJSON *path = cJSON_GetObjectItem(input, "path");
    if (!path || !cJSON_IsString(path)) {
        tool_result_set_error(&result, "Missing or invalid 'path' parameter");
        return result;
    }

    struct stat st;
    if (stat(path->valuestring, &st) != 0) {
        tool_result_set_error(&result, "File not found");
        return result;
    }

    cJSON *output = cJSON_CreateObject();
    cJSON_AddStringToObject(output, "path", path->valuestring);
    cJSON_AddNumberToObject(output, "size", (double)st.st_size);
    cJSON_AddBoolToObject(output, "is_directory", S_ISDIR(st.st_mode));
    cJSON_AddBoolToObject(output, "is_file", S_ISREG(st.st_mode));
    cJSON_AddBoolToObject(output, "is_symlink", S_ISLNK(st.st_mode));

    char date_str[32];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", localtime(&st.st_mtime));
    cJSON_AddStringToObject(output, "modified", date_str);

    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", localtime(&st.st_ctime));
    cJSON_AddStringToObject(output, "created", date_str);

    cJSON_AddNumberToObject(output, "permissions", st.st_mode & 0777);

    char *json_str = cJSON_Print(output);
    cJSON_Delete(output);

    tool_result_set_success(&result, json_str, 1);
    free(json_str);

    return result;
}

// Execute batch_rename tool
static ToolResult execute_batch_rename(cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    cJSON *paths = cJSON_GetObjectItem(input, "paths");
    cJSON *find = cJSON_GetObjectItem(input, "find");
    cJSON *replace = cJSON_GetObjectItem(input, "replace");

    if (!paths || !cJSON_IsArray(paths)) {
        tool_result_set_error(&result, "Missing or invalid 'paths' parameter (must be array)");
        return result;
    }

    if (!find || !cJSON_IsString(find)) {
        tool_result_set_error(&result, "Missing or invalid 'find' parameter");
        return result;
    }

    const char *find_str = find->valuestring;
    const char *replace_str = replace && cJSON_IsString(replace) ? replace->valuestring : "";

    int renamed_count = 0;
    int count = cJSON_GetArraySize(paths);

    for (int i = 0; i < count; i++) {
        cJSON *path_item = cJSON_GetArrayItem(paths, i);
        if (!path_item || !cJSON_IsString(path_item)) continue;

        const char *old_path = path_item->valuestring;
        const char *filename = strrchr(old_path, '/');
        filename = filename ? filename + 1 : old_path;

        // Simple find and replace in filename
        char new_name[256];
        const char *found = strstr(filename, find_str);
        if (found) {
            size_t prefix_len = (size_t)(found - filename);
            strncpy(new_name, filename, prefix_len);
            new_name[prefix_len] = '\0';
            strcat(new_name, replace_str);
            strcat(new_name, found + strlen(find_str));

            if (file_rename(old_path, new_name) == OP_SUCCESS) {
                renamed_count++;
            }
        }
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "Renamed %d of %d files", renamed_count, count);
    tool_result_set_success(&result, msg, renamed_count);

    return result;
}

// Execute semantic_search tool
static ToolResult execute_semantic_search(ToolExecutor *executor, cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    if (!executor->semantic_search) {
        tool_result_set_error(&result, "Semantic search is not available (AI engine not initialized)");
        return result;
    }

    cJSON *query = cJSON_GetObjectItem(input, "query");
    if (!query || !cJSON_IsString(query)) {
        tool_result_set_error(&result, "Missing or invalid 'query' parameter");
        return result;
    }

    cJSON *directory = cJSON_GetObjectItem(input, "directory");
    cJSON *max_results = cJSON_GetObjectItem(input, "max_results");
    cJSON *file_type_item = cJSON_GetObjectItem(input, "file_type");

    SemanticSearchOptions opts = semantic_search_default_options();
    opts.max_results = (max_results && cJSON_IsNumber(max_results)) ? (int)max_results->valuedouble : 20;
    opts.directory = (directory && cJSON_IsString(directory)) ? directory->valuestring : executor->current_dir;
    opts.min_score = 0.1f;

    // Parse file type filter
    if (file_type_item && cJSON_IsString(file_type_item)) {
        const char *ft = file_type_item->valuestring;
        if (strcmp(ft, "text") == 0) opts.file_type = FILE_TYPE_TEXT;
        else if (strcmp(ft, "code") == 0) opts.file_type = FILE_TYPE_CODE;
        else if (strcmp(ft, "document") == 0) opts.file_type = FILE_TYPE_DOCUMENT;
        else if (strcmp(ft, "image") == 0) opts.file_type = FILE_TYPE_IMAGE;
    }

    SemanticSearchResults results = semantic_search_query(executor->semantic_search, query->valuestring, &opts);

    if (!results.success) {
        tool_result_set_error(&result, results.error_message[0] ? results.error_message : "Semantic search failed");
        semantic_search_results_free(&results);
        return result;
    }

    // Build JSON output
    cJSON *output = cJSON_CreateObject();
    cJSON *matches = cJSON_CreateArray();

    for (int i = 0; i < results.count; i++) {
        SemanticSearchResult *r = &results.results[i];
        cJSON *match = cJSON_CreateObject();
        cJSON_AddStringToObject(match, "path", r->path);
        cJSON_AddStringToObject(match, "name", r->name);
        cJSON_AddNumberToObject(match, "score", r->score);
        cJSON_AddNumberToObject(match, "size", (double)r->size);
        cJSON_AddItemToArray(matches, match);
    }

    cJSON_AddItemToObject(output, "results", matches);
    cJSON_AddNumberToObject(output, "count", results.count);
    cJSON_AddStringToObject(output, "query", query->valuestring);
    cJSON_AddNumberToObject(output, "search_time_ms", results.search_time_ms);

    char *json_str = cJSON_Print(output);
    cJSON_Delete(output);

    tool_result_set_success(&result, json_str, results.count);
    free(json_str);

    semantic_search_results_free(&results);
    return result;
}

// Execute visual_search tool
static ToolResult execute_visual_search(ToolExecutor *executor, cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    if (!executor->visual_search) {
        tool_result_set_error(&result, "Visual search is not available (CLIP engine not initialized)");
        return result;
    }

    cJSON *query = cJSON_GetObjectItem(input, "query");
    if (!query || !cJSON_IsString(query)) {
        tool_result_set_error(&result, "Missing or invalid 'query' parameter");
        return result;
    }

    cJSON *directory = cJSON_GetObjectItem(input, "directory");
    cJSON *max_results = cJSON_GetObjectItem(input, "max_results");

    VisualSearchOptions opts = visual_search_default_options();
    opts.max_results = (max_results && cJSON_IsNumber(max_results)) ? (int)max_results->valuedouble : 20;
    opts.directory = (directory && cJSON_IsString(directory)) ? directory->valuestring : executor->current_dir;
    opts.min_score = 0.1f;

    VisualSearchResults results = visual_search_query(executor->visual_search, query->valuestring, &opts);

    if (!results.success) {
        tool_result_set_error(&result, results.error_message[0] ? results.error_message : "Visual search failed");
        visual_search_results_free(&results);
        return result;
    }

    // Build JSON output
    cJSON *output = cJSON_CreateObject();
    cJSON *matches = cJSON_CreateArray();

    for (int i = 0; i < results.count; i++) {
        VisualSearchResult *r = &results.results[i];
        cJSON *match = cJSON_CreateObject();
        cJSON_AddStringToObject(match, "path", r->path);
        cJSON_AddStringToObject(match, "name", r->name);
        cJSON_AddNumberToObject(match, "score", r->score);
        cJSON_AddNumberToObject(match, "width", r->width);
        cJSON_AddNumberToObject(match, "height", r->height);
        cJSON_AddNumberToObject(match, "size", (double)r->size);
        cJSON_AddItemToArray(matches, match);
    }

    cJSON_AddItemToObject(output, "results", matches);
    cJSON_AddNumberToObject(output, "count", results.count);
    cJSON_AddStringToObject(output, "query", query->valuestring);
    cJSON_AddNumberToObject(output, "search_time_ms", results.search_time_ms);

    char *json_str = cJSON_Print(output);
    cJSON_Delete(output);

    tool_result_set_success(&result, json_str, results.count);
    free(json_str);

    visual_search_results_free(&results);
    return result;
}

// Execute similar_images tool
static ToolResult execute_similar_images(ToolExecutor *executor, cJSON *input)
{
    ToolResult result;
    tool_result_init(&result);

    if (!executor->visual_search) {
        tool_result_set_error(&result, "Similar images search is not available (CLIP engine not initialized)");
        return result;
    }

    cJSON *image_path = cJSON_GetObjectItem(input, "image_path");
    if (!image_path || !cJSON_IsString(image_path)) {
        tool_result_set_error(&result, "Missing or invalid 'image_path' parameter");
        return result;
    }

    cJSON *directory = cJSON_GetObjectItem(input, "directory");
    cJSON *max_results = cJSON_GetObjectItem(input, "max_results");

    VisualSearchOptions opts = visual_search_default_options();
    opts.max_results = (max_results && cJSON_IsNumber(max_results)) ? (int)max_results->valuedouble : 20;
    opts.directory = (directory && cJSON_IsString(directory)) ? directory->valuestring : executor->current_dir;
    opts.min_score = 0.1f;

    VisualSearchResults results = visual_search_similar(executor->visual_search, image_path->valuestring, &opts);

    if (!results.success) {
        tool_result_set_error(&result, results.error_message[0] ? results.error_message : "Similar images search failed");
        visual_search_results_free(&results);
        return result;
    }

    // Build JSON output
    cJSON *output = cJSON_CreateObject();
    cJSON *matches = cJSON_CreateArray();

    for (int i = 0; i < results.count; i++) {
        VisualSearchResult *r = &results.results[i];
        cJSON *match = cJSON_CreateObject();
        cJSON_AddStringToObject(match, "path", r->path);
        cJSON_AddStringToObject(match, "name", r->name);
        cJSON_AddNumberToObject(match, "score", r->score);
        cJSON_AddNumberToObject(match, "width", r->width);
        cJSON_AddNumberToObject(match, "height", r->height);
        cJSON_AddNumberToObject(match, "size", (double)r->size);
        cJSON_AddItemToArray(matches, match);
    }

    cJSON_AddItemToObject(output, "results", matches);
    cJSON_AddNumberToObject(output, "count", results.count);
    cJSON_AddStringToObject(output, "reference_image", image_path->valuestring);
    cJSON_AddNumberToObject(output, "search_time_ms", results.search_time_ms);

    char *json_str = cJSON_Print(output);
    cJSON_Delete(output);

    tool_result_set_success(&result, json_str, results.count);
    free(json_str);

    visual_search_results_free(&results);
    return result;
}

// Generate unique filename if file already exists
static void generate_unique_filename(const char *base_path, char *unique_path, size_t size)
{
    strncpy(unique_path, base_path, size - 1);
    unique_path[size - 1] = '\0';

    struct stat st;
    if (stat(unique_path, &st) != 0) {
        return; // File doesn't exist, use original path
    }

    // Find extension
    const char *ext = strrchr(base_path, '.');
    char base_without_ext[1024];
    if (ext && ext != base_path) {
        size_t base_len = (size_t)(ext - base_path);
        strncpy(base_without_ext, base_path, base_len);
        base_without_ext[base_len] = '\0';
    } else {
        strncpy(base_without_ext, base_path, sizeof(base_without_ext) - 1);
        base_without_ext[sizeof(base_without_ext) - 1] = '\0';
        ext = "";
    }

    // Try adding numbers until we find a unique name
    for (int i = 1; i < 1000; i++) {
        snprintf(unique_path, size, "%s_%d%s", base_without_ext, i, ext);
        if (stat(unique_path, &st) != 0) {
            return; // Found unique name
        }
    }
}

// Execute image_generate tool
static ToolResult execute_image_generate(ToolExecutor *executor, cJSON *input)
{
    TOOL_LOG("image_generate: starting");

    ToolResult result;
    tool_result_init(&result);

    if (!executor->gemini_client) {
        TOOL_LOG("image_generate: no Gemini client configured");
        tool_result_set_error(&result,
            "Image generation not available (GEMINI_API_KEY not configured)");
        return result;
    }

    // Get prompt (required)
    cJSON *prompt_item = cJSON_GetObjectItem(input, "prompt");
    if (!prompt_item || !cJSON_IsString(prompt_item) ||
        strlen(prompt_item->valuestring) == 0) {
        tool_result_set_error(&result, "Missing or empty 'prompt' parameter");
        return result;
    }

    // Get optional filename
    cJSON *filename_item = cJSON_GetObjectItem(input, "filename");
    const char *base_filename = (filename_item && cJSON_IsString(filename_item))
        ? filename_item->valuestring
        : "generated_image";

    TOOL_LOG("image_generate: prompt='%.50s%s', filename='%s', dir='%s'",
             prompt_item->valuestring,
             strlen(prompt_item->valuestring) > 50 ? "..." : "",
             base_filename, executor->current_dir);

    // Get optional model preference
    cJSON *model_item = cJSON_GetObjectItem(input, "model");
    if (model_item && cJSON_IsString(model_item)) {
        if (strcmp(model_item->valuestring, "quality") == 0) {
            gemini_client_set_model(executor->gemini_client, GEMINI_QUALITY_MODEL);
        } else {
            gemini_client_set_model(executor->gemini_client, GEMINI_DEFAULT_MODEL);
        }
    }

    // Build request
    GeminiImageRequest req;
    gemini_request_init(&req);
    gemini_request_set_prompt(&req, prompt_item->valuestring);

    // Generate image (gemini_client logs details if GEMINI_DEBUG is enabled)
    GeminiImageResponse resp;
    gemini_response_init(&resp);

    bool success = gemini_generate_image(executor->gemini_client, &req, &resp);

    if (!success || resp.result_type != GEMINI_RESULT_SUCCESS) {
        char err_msg[512];
        switch (resp.result_type) {
            case GEMINI_RESULT_INVALID_KEY:
                snprintf(err_msg, sizeof(err_msg),
                    "Invalid Gemini API key. Please check your GEMINI_API_KEY configuration.");
                break;
            case GEMINI_RESULT_RATE_LIMIT:
                snprintf(err_msg, sizeof(err_msg),
                    "Rate limit exceeded. Please wait a moment and try again.");
                break;
            case GEMINI_RESULT_CONTENT_FILTERED:
                snprintf(err_msg, sizeof(err_msg),
                    "Image generation was blocked due to content policy. Try a different prompt.");
                break;
            default:
                snprintf(err_msg, sizeof(err_msg), "Image generation failed: %s",
                    resp.error[0] ? resp.error : gemini_result_to_string(resp.result_type));
        }
        TOOL_LOG("image_generate: failed - %s", err_msg);
        tool_result_set_error(&result, err_msg);
        gemini_response_cleanup(&resp);
        return result;
    }

    // Build output path in current directory
    const char *extension = gemini_format_to_extension(resp.format);
    char output_path[2048];
    snprintf(output_path, sizeof(output_path), "%s/%s%s",
             executor->current_dir, base_filename, extension);

    // Handle filename conflicts
    char unique_path[2048];
    generate_unique_filename(output_path, unique_path, sizeof(unique_path));

    TOOL_LOG("image_generate: saving %zu bytes to '%s'", resp.image_size, unique_path);

    // Save image
    if (!gemini_save_image(&resp, unique_path)) {
        TOOL_LOG("image_generate: failed to save to disk");
        tool_result_set_error(&result, "Failed to save generated image to disk");
        gemini_response_cleanup(&resp);
        return result;
    }

    // Extract just the filename for the success message
    const char *saved_filename = strrchr(unique_path, '/');
    saved_filename = saved_filename ? saved_filename + 1 : unique_path;

    // Success message
    char success_msg[512];
    snprintf(success_msg, sizeof(success_msg),
             "Generated image saved as '%s' (%zu bytes)",
             saved_filename, resp.image_size);
    tool_result_set_success(&result, success_msg, 1);

    TOOL_LOG("image_generate: completed successfully");
    gemini_response_cleanup(&resp);
    return result;
}

ToolResult tool_executor_execute(ToolExecutor *executor, const char *tool_name, const char *input_json)
{
    ToolResult result;
    tool_result_init(&result);

    if (!executor || !tool_name) {
        tool_result_set_error(&result, "Invalid executor or tool name");
        return result;
    }

    cJSON *input = cJSON_Parse(input_json);
    if (!input) {
        tool_result_set_error(&result, "Failed to parse input JSON");
        return result;
    }

    if (strcmp(tool_name, "file_list") == 0) {
        result = execute_file_list(executor, input);
    } else if (strcmp(tool_name, "file_move") == 0) {
        result = execute_file_move(input);
    } else if (strcmp(tool_name, "file_copy") == 0) {
        result = execute_file_copy(input);
    } else if (strcmp(tool_name, "file_delete") == 0) {
        result = execute_file_delete(input);
    } else if (strcmp(tool_name, "file_create") == 0) {
        result = execute_file_create(input);
    } else if (strcmp(tool_name, "file_rename") == 0) {
        result = execute_file_rename(input);
    } else if (strcmp(tool_name, "file_search") == 0) {
        result = execute_file_search(executor, input);
    } else if (strcmp(tool_name, "file_metadata") == 0) {
        result = execute_file_metadata(input);
    } else if (strcmp(tool_name, "batch_rename") == 0) {
        result = execute_batch_rename(input);
    } else if (strcmp(tool_name, "semantic_search") == 0) {
        result = execute_semantic_search(executor, input);
    } else if (strcmp(tool_name, "visual_search") == 0) {
        result = execute_visual_search(executor, input);
    } else if (strcmp(tool_name, "similar_images") == 0) {
        result = execute_similar_images(executor, input);
    } else if (strcmp(tool_name, "image_generate") == 0) {
        result = execute_image_generate(executor, input);
    } else {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "Unknown tool: %s", tool_name);
        tool_result_set_error(&result, err_buf);
    }

    cJSON_Delete(input);
    return result;
}

bool tool_executor_prepare(ToolExecutor *executor, const char *tool_name,
                           const char *tool_id, const char *input_json,
                           PendingOperation *pending)
{
    if (!executor || !tool_name || !pending) return false;

    memset(pending, 0, sizeof(PendingOperation));
    strncpy(pending->tool_id, tool_id ? tool_id : "", 63);
    strncpy(pending->tool_name, tool_name, 63);
    strncpy(pending->input_json, input_json ? input_json : "{}", sizeof(pending->input_json) - 1);

    const ToolDefinition *tool = tool_registry_find(executor->registry, tool_name);
    pending->requires_confirmation = tool ? tool->requires_confirmation : true;

    // Generate description
    tool_executor_describe_operation(tool_name, input_json, pending->description, sizeof(pending->description));

    // Parse input for details
    cJSON *input = cJSON_Parse(input_json);
    if (input) {
        char *details = cJSON_Print(input);
        if (details) {
            strncpy(pending->details, details, sizeof(pending->details) - 1);
            free(details);
        }
        cJSON_Delete(input);
    }

    return true;
}

ToolResult tool_executor_confirm(ToolExecutor *executor, const PendingOperation *pending)
{
    if (!executor || !pending) {
        ToolResult result;
        tool_result_init(&result);
        tool_result_set_error(&result, "Invalid executor or pending operation");
        return result;
    }

    return tool_executor_execute(executor, pending->tool_name, pending->input_json);
}

void tool_executor_cancel(PendingOperation *pending)
{
    if (pending) {
        memset(pending, 0, sizeof(PendingOperation));
    }
}

const char *tool_executor_describe_operation(const char *tool_name, const char *input_json,
                                              char *buffer, size_t buffer_size)
{
    if (!tool_name || !buffer || buffer_size == 0) return NULL;

    cJSON *input = cJSON_Parse(input_json);

    if (strcmp(tool_name, "file_list") == 0) {
        cJSON *path = input ? cJSON_GetObjectItem(input, "path") : NULL;
        snprintf(buffer, buffer_size, "List files in %s",
                 path && cJSON_IsString(path) ? path->valuestring : "directory");
    } else if (strcmp(tool_name, "file_move") == 0) {
        cJSON *source = input ? cJSON_GetObjectItem(input, "source") : NULL;
        cJSON *dest = input ? cJSON_GetObjectItem(input, "destination") : NULL;
        snprintf(buffer, buffer_size, "Move %s to %s",
                 source && cJSON_IsString(source) ? source->valuestring : "files",
                 dest && cJSON_IsString(dest) ? dest->valuestring : "destination");
    } else if (strcmp(tool_name, "file_copy") == 0) {
        cJSON *source = input ? cJSON_GetObjectItem(input, "source") : NULL;
        cJSON *dest = input ? cJSON_GetObjectItem(input, "destination") : NULL;
        snprintf(buffer, buffer_size, "Copy %s to %s",
                 source && cJSON_IsString(source) ? source->valuestring : "files",
                 dest && cJSON_IsString(dest) ? dest->valuestring : "destination");
    } else if (strcmp(tool_name, "file_delete") == 0) {
        snprintf(buffer, buffer_size, "Move files to Trash");
    } else if (strcmp(tool_name, "file_create") == 0) {
        cJSON *path = input ? cJSON_GetObjectItem(input, "path") : NULL;
        cJSON *is_dir = input ? cJSON_GetObjectItem(input, "is_directory") : NULL;
        cJSON *content = input ? cJSON_GetObjectItem(input, "content") : NULL;
        bool has_content = content && cJSON_IsString(content) && content->valuestring[0] != '\0';
        if (is_dir && cJSON_IsTrue(is_dir)) {
            snprintf(buffer, buffer_size, "Create directory '%s'",
                     path && cJSON_IsString(path) ? path->valuestring : "");
        } else if (has_content) {
            snprintf(buffer, buffer_size, "Create file '%s' with content",
                     path && cJSON_IsString(path) ? path->valuestring : "");
        } else {
            snprintf(buffer, buffer_size, "Create file '%s'",
                     path && cJSON_IsString(path) ? path->valuestring : "");
        }
    } else if (strcmp(tool_name, "file_rename") == 0) {
        cJSON *new_name = input ? cJSON_GetObjectItem(input, "new_name") : NULL;
        snprintf(buffer, buffer_size, "Rename to '%s'",
                 new_name && cJSON_IsString(new_name) ? new_name->valuestring : "");
    } else if (strcmp(tool_name, "batch_rename") == 0) {
        cJSON *paths = input ? cJSON_GetObjectItem(input, "paths") : NULL;
        int count = paths && cJSON_IsArray(paths) ? cJSON_GetArraySize(paths) : 0;
        snprintf(buffer, buffer_size, "Rename %d files", count);
    } else if (strcmp(tool_name, "semantic_search") == 0) {
        cJSON *query = input ? cJSON_GetObjectItem(input, "query") : NULL;
        snprintf(buffer, buffer_size, "Search for files matching '%s'",
                 query && cJSON_IsString(query) ? query->valuestring : "query");
    } else if (strcmp(tool_name, "visual_search") == 0) {
        cJSON *query = input ? cJSON_GetObjectItem(input, "query") : NULL;
        snprintf(buffer, buffer_size, "Search for images matching '%s'",
                 query && cJSON_IsString(query) ? query->valuestring : "query");
    } else if (strcmp(tool_name, "similar_images") == 0) {
        cJSON *image_path = input ? cJSON_GetObjectItem(input, "image_path") : NULL;
        const char *filename = image_path && cJSON_IsString(image_path) ? strrchr(image_path->valuestring, '/') : NULL;
        filename = filename ? filename + 1 : (image_path ? image_path->valuestring : "image");
        snprintf(buffer, buffer_size, "Find images similar to '%s'", filename);
    } else if (strcmp(tool_name, "image_generate") == 0) {
        cJSON *prompt = input ? cJSON_GetObjectItem(input, "prompt") : NULL;
        cJSON *filename = input ? cJSON_GetObjectItem(input, "filename") : NULL;
        if (filename && cJSON_IsString(filename)) {
            snprintf(buffer, buffer_size, "Generate image '%s' from prompt", filename->valuestring);
        } else if (prompt && cJSON_IsString(prompt)) {
            // Truncate long prompts
            char truncated[64];
            strncpy(truncated, prompt->valuestring, 50);
            truncated[50] = '\0';
            if (strlen(prompt->valuestring) > 50) {
                strcat(truncated, "...");
            }
            snprintf(buffer, buffer_size, "Generate image: '%s'", truncated);
        } else {
            snprintf(buffer, buffer_size, "Generate image from prompt");
        }
    } else {
        snprintf(buffer, buffer_size, "Execute %s", tool_name);
    }

    if (input) cJSON_Delete(input);
    return buffer;
}

#include "smart_rename.h"
#include "../api/claude_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include <libgen.h>

// Undo history for last batch rename
static struct {
    char original_paths[RENAME_MAX_FILES][1024];
    char new_paths[RENAME_MAX_FILES][1024];
    int count;
    bool valid;
} undo_history = {0};

// Initialize configuration with defaults
void smart_rename_config_init(SmartRenameConfig *config)
{
    if (!config) return;

    memset(config, 0, sizeof(SmartRenameConfig));
    config->default_format = RENAME_FORMAT_SNAKE_CASE;
    config->auto_detect_format = true;
    config->max_content_bytes = 4096;
    config->max_concurrent = 3;
    config->min_confidence = 0.7f;
}

// Create batch rename request
BatchRenameRequest *smart_rename_request_create(void)
{
    BatchRenameRequest *request = calloc(1, sizeof(BatchRenameRequest));
    if (!request) return NULL;

    request->capacity = 32;
    request->suggestions = calloc(request->capacity, sizeof(RenameSuggestion));
    if (!request->suggestions) {
        free(request);
        return NULL;
    }

    request->format = RENAME_FORMAT_ORIGINAL;
    request->preserve_extension = true;
    request->use_content = true;
    request->use_metadata = true;

    return request;
}

// Free batch rename request
void smart_rename_request_free(BatchRenameRequest *request)
{
    if (!request) return;
    free(request->suggestions);
    free(request);
}

// Add file to batch rename request
bool smart_rename_request_add_file(BatchRenameRequest *request, const char *path)
{
    if (!request || !path) return false;

    // Check if file exists
    struct stat st;
    if (stat(path, &st) != 0) return false;

    // Expand capacity if needed
    if (request->count >= request->capacity) {
        int new_capacity = request->capacity * 2;
        if (new_capacity > RENAME_MAX_FILES) new_capacity = RENAME_MAX_FILES;
        if (request->count >= new_capacity) return false;

        RenameSuggestion *new_suggestions = realloc(request->suggestions,
                                                     new_capacity * sizeof(RenameSuggestion));
        if (!new_suggestions) return false;
        request->suggestions = new_suggestions;
        request->capacity = new_capacity;
    }

    RenameSuggestion *suggestion = &request->suggestions[request->count];
    memset(suggestion, 0, sizeof(RenameSuggestion));

    // Store full path
    strncpy(suggestion->original_path, path, sizeof(suggestion->original_path) - 1);

    // Extract filename and extension
    char *path_copy = strdup(path);
    char *filename = basename(path_copy);

    const char *ext = strrchr(filename, '.');
    if (ext && ext != filename) {
        strncpy(suggestion->extension, ext, sizeof(suggestion->extension) - 1);
        size_t name_len = ext - filename;
        if (name_len >= sizeof(suggestion->original_name)) {
            name_len = sizeof(suggestion->original_name) - 1;
        }
        strncpy(suggestion->original_name, filename, name_len);
    } else {
        strncpy(suggestion->original_name, filename, sizeof(suggestion->original_name) - 1);
    }

    free(path_copy);

    // Default suggested name to original
    strncpy(suggestion->suggested_name, suggestion->original_name, sizeof(suggestion->suggested_name) - 1);
    suggestion->status = RENAME_STATUS_OK;

    request->count++;
    return true;
}

// Convert string to snake_case
static void to_snake_case(const char *input, char *output, size_t output_size)
{
    size_t j = 0;
    bool prev_upper = false;

    for (size_t i = 0; input[i] && j < output_size - 1; i++) {
        char c = input[i];

        if (c == ' ' || c == '-' || c == '_') {
            if (j > 0 && output[j-1] != '_') {
                output[j++] = '_';
            }
            prev_upper = false;
        } else if (isupper(c)) {
            if (j > 0 && !prev_upper && output[j-1] != '_') {
                output[j++] = '_';
            }
            if (j < output_size - 1) {
                output[j++] = tolower(c);
            }
            prev_upper = true;
        } else if (isalnum(c)) {
            output[j++] = tolower(c);
            prev_upper = false;
        }
    }

    // Remove trailing underscore
    while (j > 0 && output[j-1] == '_') j--;
    output[j] = '\0';
}

// Convert string to kebab-case
static void to_kebab_case(const char *input, char *output, size_t output_size)
{
    to_snake_case(input, output, output_size);
    for (size_t i = 0; output[i]; i++) {
        if (output[i] == '_') output[i] = '-';
    }
}

// Convert string to camelCase
static void to_camel_case(const char *input, char *output, size_t output_size)
{
    size_t j = 0;
    bool capitalize_next = false;
    bool first = true;

    for (size_t i = 0; input[i] && j < output_size - 1; i++) {
        char c = input[i];

        if (c == ' ' || c == '-' || c == '_') {
            capitalize_next = true;
        } else if (isalnum(c)) {
            if (capitalize_next && !first) {
                output[j++] = toupper(c);
            } else if (first) {
                output[j++] = tolower(c);
            } else {
                output[j++] = c;
            }
            capitalize_next = false;
            first = false;
        }
    }
    output[j] = '\0';
}

// Convert string to PascalCase
static void to_pascal_case(const char *input, char *output, size_t output_size)
{
    to_camel_case(input, output, output_size);
    if (output[0]) {
        output[0] = toupper(output[0]);
    }
}

// Convert string to Title Case
static void to_title_case(const char *input, char *output, size_t output_size)
{
    size_t j = 0;
    bool capitalize_next = true;

    for (size_t i = 0; input[i] && j < output_size - 1; i++) {
        char c = input[i];

        if (c == '_' || c == '-') {
            output[j++] = ' ';
            capitalize_next = true;
        } else if (c == ' ') {
            output[j++] = ' ';
            capitalize_next = true;
        } else if (isalnum(c)) {
            if (capitalize_next) {
                output[j++] = toupper(c);
            } else {
                output[j++] = tolower(c);
            }
            capitalize_next = false;
        } else {
            output[j++] = c;
        }
    }
    output[j] = '\0';
}

// Convert name to specified format
void smart_rename_format_name(const char *name, RenameFormat format, char *output, size_t output_size)
{
    if (!name || !output || output_size == 0) return;

    switch (format) {
        case RENAME_FORMAT_SNAKE_CASE:
            to_snake_case(name, output, output_size);
            break;
        case RENAME_FORMAT_KEBAB_CASE:
            to_kebab_case(name, output, output_size);
            break;
        case RENAME_FORMAT_CAMEL_CASE:
            to_camel_case(name, output, output_size);
            break;
        case RENAME_FORMAT_PASCAL_CASE:
            to_pascal_case(name, output, output_size);
            break;
        case RENAME_FORMAT_TITLE_CASE:
            to_title_case(name, output, output_size);
            break;
        default:
            strncpy(output, name, output_size - 1);
            output[output_size - 1] = '\0';
            break;
    }
}

// Read file content preview
static bool read_file_preview(const char *path, char *buffer, size_t max_size)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;

    size_t len = fread(buffer, 1, max_size - 1, f);
    buffer[len] = '\0';
    fclose(f);
    return len > 0;
}

// Build prompt for Claude to suggest names
static void build_rename_prompt(const RenameSuggestion *suggestions, int count,
                                 bool use_content, char *prompt, size_t prompt_size)
{
    char *p = prompt;
    size_t remaining = prompt_size;

    int written = snprintf(p, remaining,
        "You are a file naming assistant. Suggest better, more descriptive names for these files.\n"
        "Rules:\n"
        "- Use snake_case format\n"
        "- Keep names concise but descriptive\n"
        "- Preserve file type/purpose in name\n"
        "- Don't include extension in suggestion\n\n"
        "Files:\n");

    p += written;
    remaining -= written;

    for (int i = 0; i < count && remaining > 512; i++) {
        written = snprintf(p, remaining, "%d. \"%s%s\"",
                           i + 1, suggestions[i].original_name, suggestions[i].extension);
        p += written;
        remaining -= written;

        // Add content preview if available and requested
        if (use_content) {
            char content[512];
            if (read_file_preview(suggestions[i].original_path, content, sizeof(content))) {
                // Truncate and sanitize content
                for (int j = 0; content[j]; j++) {
                    if (content[j] == '\n' || content[j] == '\r') content[j] = ' ';
                }
                if (strlen(content) > 100) {
                    content[100] = '\0';
                    strcat(content, "...");
                }
                written = snprintf(p, remaining, " (content: %s)", content);
                p += written;
                remaining -= written;
            }
        }

        written = snprintf(p, remaining, "\n");
        p += written;
        remaining -= written;
    }

    snprintf(p, remaining,
        "\nRespond with ONLY a JSON array of suggested names, one per file:\n"
        "[{\"index\": 1, \"name\": \"suggested_name\", \"reason\": \"brief reason\"}, ...]\n");
}

// Parse Claude's response for suggestions
static int parse_rename_response(const char *response, RenameSuggestion *suggestions, int max_count)
{
    // Simple JSON parsing - find array and extract names
    const char *start = strchr(response, '[');
    if (!start) return 0;

    int parsed = 0;
    const char *p = start;

    while (parsed < max_count) {
        // Find next object
        const char *obj_start = strchr(p, '{');
        if (!obj_start) break;

        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;

        // Extract index
        const char *idx_key = strstr(obj_start, "\"index\"");
        int index = 0;
        if (idx_key && idx_key < obj_end) {
            const char *colon = strchr(idx_key, ':');
            if (colon) {
                index = atoi(colon + 1);
            }
        }

        // Extract name
        const char *name_key = strstr(obj_start, "\"name\"");
        if (name_key && name_key < obj_end) {
            const char *name_start = strchr(name_key + 6, '"');
            if (name_start) {
                name_start++;
                const char *name_end = strchr(name_start, '"');
                if (name_end) {
                    size_t name_len = name_end - name_start;
                    if (name_len < RENAME_MAX_NAME_LEN && index > 0 && index <= max_count) {
                        RenameSuggestion *s = &suggestions[index - 1];
                        strncpy(s->suggested_name, name_start, name_len);
                        s->suggested_name[name_len] = '\0';
                        s->confidence = 0.85f;
                        parsed++;
                    }
                }
            }
        }

        // Extract reason
        const char *reason_key = strstr(obj_start, "\"reason\"");
        if (reason_key && reason_key < obj_end && index > 0 && index <= max_count) {
            const char *reason_start = strchr(reason_key + 8, '"');
            if (reason_start) {
                reason_start++;
                const char *reason_end = strchr(reason_start, '"');
                if (reason_end) {
                    size_t reason_len = reason_end - reason_start;
                    if (reason_len < sizeof(suggestions[0].reason)) {
                        RenameSuggestion *s = &suggestions[index - 1];
                        strncpy(s->reason, reason_start, reason_len);
                        s->reason[reason_len] = '\0';
                    }
                }
            }
        }

        p = obj_end + 1;
    }

    return parsed;
}

// Generate AI suggestions for files
SmartRenameStatus smart_rename_generate_suggestions(BatchRenameRequest *request,
                                                     const SmartRenameConfig *config)
{
    if (!request || !config || request->count == 0) {
        return RENAME_STATUS_NOT_INITIALIZED;
    }

    // Check for API key
    if (strlen(config->api_key) == 0) {
        // Fall back to simple pattern-based renaming
        return smart_rename_apply_pattern(request, "%name%");
    }

    // Build prompt
    char prompt[8192];
    build_rename_prompt(request->suggestions, request->count, request->use_content, prompt, sizeof(prompt));

    // Create Claude client and send request
    ClaudeClient *client = claude_client_create(config->api_key);
    if (!client) {
        return RENAME_STATUS_API_ERROR;
    }

    ClaudeMessageRequest req;
    ClaudeMessageResponse resp;
    claude_request_init(&req);
    claude_response_init(&resp);

    claude_request_set_system_prompt(&req, "You are a helpful file naming assistant. Respond only with valid JSON.");
    claude_request_add_user_message(&req, prompt);

    bool success = claude_send_message(client, &req, &resp);

    if (success && resp.stop_reason != CLAUDE_STOP_ERROR) {
        parse_rename_response(resp.content, request->suggestions, request->count);
    }

    claude_request_cleanup(&req);
    claude_response_cleanup(&resp);
    claude_client_destroy(client);

    // Apply format if specified
    if (request->format != RENAME_FORMAT_ORIGINAL) {
        for (int i = 0; i < request->count; i++) {
            char formatted[RENAME_MAX_NAME_LEN];
            smart_rename_format_name(request->suggestions[i].suggested_name, request->format,
                                      formatted, sizeof(formatted));
            strncpy(request->suggestions[i].suggested_name, formatted,
                    sizeof(request->suggestions[i].suggested_name) - 1);
        }
    }

    return success ? RENAME_STATUS_OK : RENAME_STATUS_API_ERROR;
}

// Generate suggestions using a pattern
SmartRenameStatus smart_rename_apply_pattern(BatchRenameRequest *request, const char *pattern)
{
    if (!request || !pattern) return RENAME_STATUS_NOT_INITIALIZED;

    for (int i = 0; i < request->count; i++) {
        smart_rename_expand_pattern(pattern, request->suggestions[i].original_path, i + 1,
                                     request->suggestions[i].suggested_name,
                                     sizeof(request->suggestions[i].suggested_name));

        // Apply format if specified
        if (request->format != RENAME_FORMAT_ORIGINAL) {
            char formatted[RENAME_MAX_NAME_LEN];
            smart_rename_format_name(request->suggestions[i].suggested_name, request->format,
                                      formatted, sizeof(formatted));
            strncpy(request->suggestions[i].suggested_name, formatted,
                    sizeof(request->suggestions[i].suggested_name) - 1);
        }

        request->suggestions[i].confidence = 1.0f;
        strncpy(request->suggestions[i].reason, "Pattern applied", sizeof(request->suggestions[i].reason) - 1);
    }

    return RENAME_STATUS_OK;
}

// Expand pattern with actual values
void smart_rename_expand_pattern(const char *pattern, const char *path, int index,
                                  char *output, size_t output_size)
{
    if (!pattern || !path || !output || output_size == 0) return;

    struct stat st;
    if (stat(path, &st) != 0) {
        strncpy(output, pattern, output_size - 1);
        output[output_size - 1] = '\0';
        return;
    }

    // Extract original name without extension
    char *path_copy = strdup(path);
    char *filename = basename(path_copy);
    char name[256] = {0};
    const char *ext = strrchr(filename, '.');
    if (ext && ext != filename) {
        size_t len = ext - filename;
        strncpy(name, filename, len < sizeof(name) ? len : sizeof(name) - 1);
    } else {
        strncpy(name, filename, sizeof(name) - 1);
    }
    free(path_copy);

    // Get date/time
    struct tm *tm = localtime(&st.st_mtime);
    char date[32], time_str[32];
    strftime(date, sizeof(date), "%Y-%m-%d", tm);
    strftime(time_str, sizeof(time_str), "%H%M%S", tm);

    // Get extension (without dot)
    char ext_str[32] = {0};
    if (ext && strlen(ext) > 1) {
        strncpy(ext_str, ext + 1, sizeof(ext_str) - 1);
    }

    // Get file type
    const char *type = "file";
    if (S_ISDIR(st.st_mode)) type = "folder";
    else if (ext) {
        if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".png") == 0) type = "image";
        else if (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".mov") == 0) type = "video";
        else if (strcasecmp(ext, ".pdf") == 0) type = "document";
        else if (strcasecmp(ext, ".c") == 0 || strcasecmp(ext, ".py") == 0) type = "code";
    }

    // Get size string
    char size_str[32];
    if (st.st_size < 1024) {
        snprintf(size_str, sizeof(size_str), "%lldB", st.st_size);
    } else if (st.st_size < 1024 * 1024) {
        snprintf(size_str, sizeof(size_str), "%lldKB", st.st_size / 1024);
    } else {
        snprintf(size_str, sizeof(size_str), "%lldMB", st.st_size / (1024 * 1024));
    }

    // Index string
    char index_str[16];
    snprintf(index_str, sizeof(index_str), "%03d", index);

    // Expand pattern
    char *out = output;
    size_t remaining = output_size - 1;
    const char *p = pattern;

    while (*p && remaining > 0) {
        if (*p == '%') {
            const char *end = strchr(p + 1, '%');
            if (end) {
                size_t token_len = end - p - 1;
                char token[32] = {0};
                strncpy(token, p + 1, token_len < sizeof(token) ? token_len : sizeof(token) - 1);

                const char *replacement = NULL;
                if (strcmp(token, "name") == 0) replacement = name;
                else if (strcmp(token, "date") == 0) replacement = date;
                else if (strcmp(token, "time") == 0) replacement = time_str;
                else if (strcmp(token, "index") == 0) replacement = index_str;
                else if (strcmp(token, "ext") == 0) replacement = ext_str;
                else if (strcmp(token, "type") == 0) replacement = type;
                else if (strcmp(token, "size") == 0) replacement = size_str;

                if (replacement) {
                    size_t len = strlen(replacement);
                    if (len > remaining) len = remaining;
                    memcpy(out, replacement, len);
                    out += len;
                    remaining -= len;
                    p = end + 1;
                    continue;
                }
            }
        }

        *out++ = *p++;
        remaining--;
    }
    *out = '\0';
}

// Preview rename operation
bool smart_rename_preview(BatchRenameRequest *request)
{
    if (!request) return false;

    bool has_conflicts = false;

    for (int i = 0; i < request->count; i++) {
        RenameSuggestion *s = &request->suggestions[i];

        // Build new full path
        char dir[1024];
        strncpy(dir, s->original_path, sizeof(dir) - 1);
        char *last_slash = strrchr(dir, '/');
        if (last_slash) *last_slash = '\0';

        char new_path[1024];
        if (request->preserve_extension) {
            snprintf(new_path, sizeof(new_path), "%s/%s%s", dir, s->suggested_name, s->extension);
        } else {
            snprintf(new_path, sizeof(new_path), "%s/%s", dir, s->suggested_name);
        }

        // Check if new path would conflict
        if (strcmp(new_path, s->original_path) != 0) {
            struct stat st;
            if (stat(new_path, &st) == 0) {
                s->has_conflict = true;
                has_conflicts = true;
            } else {
                s->has_conflict = false;
            }
        } else {
            s->has_conflict = false;
        }
    }

    return !has_conflicts;
}

// Execute rename operation
BatchRenameResult smart_rename_execute(BatchRenameRequest *request)
{
    BatchRenameResult result = {0};
    if (!request) {
        result.status = RENAME_STATUS_NOT_INITIALIZED;
        return result;
    }

    result.total_files = request->count;

    // Clear undo history
    undo_history.count = 0;
    undo_history.valid = false;

    for (int i = 0; i < request->count; i++) {
        RenameSuggestion *s = &request->suggestions[i];

        if (!s->accepted) {
            result.skipped_count++;
            continue;
        }

        if (s->has_conflict) {
            result.skipped_count++;
            continue;
        }

        // Build new path
        char dir[1024];
        strncpy(dir, s->original_path, sizeof(dir) - 1);
        char *last_slash = strrchr(dir, '/');
        if (last_slash) *last_slash = '\0';

        char new_path[1024];
        if (request->preserve_extension) {
            snprintf(new_path, sizeof(new_path), "%s/%s%s", dir, s->suggested_name, s->extension);
        } else {
            snprintf(new_path, sizeof(new_path), "%s/%s", dir, s->suggested_name);
        }

        // Skip if same name
        if (strcmp(new_path, s->original_path) == 0) {
            result.skipped_count++;
            continue;
        }

        // Perform rename
        if (rename(s->original_path, new_path) == 0) {
            result.renamed_count++;

            // Store in undo history
            if (undo_history.count < RENAME_MAX_FILES) {
                strncpy(undo_history.original_paths[undo_history.count], s->original_path, 1023);
                strncpy(undo_history.new_paths[undo_history.count], new_path, 1023);
                undo_history.count++;
            }

            // Learn from this rename
            smart_rename_learn(s->original_name, s->suggested_name);
        } else {
            result.error_count++;
            s->status = RENAME_STATUS_FILE_ERROR;
        }
    }

    undo_history.valid = (undo_history.count > 0);
    result.status = (result.error_count == 0) ? RENAME_STATUS_OK : RENAME_STATUS_FILE_ERROR;
    return result;
}

// Undo last batch rename
bool smart_rename_undo(void)
{
    if (!undo_history.valid || undo_history.count == 0) {
        return false;
    }

    int undone = 0;
    for (int i = 0; i < undo_history.count; i++) {
        if (rename(undo_history.new_paths[i], undo_history.original_paths[i]) == 0) {
            undone++;
        }
    }

    undo_history.valid = false;
    undo_history.count = 0;
    return undone > 0;
}

// Accept suggestion
void smart_rename_accept(RenameSuggestion *suggestion)
{
    if (suggestion) suggestion->accepted = true;
}

// Reject suggestion
void smart_rename_reject(RenameSuggestion *suggestion)
{
    if (suggestion) suggestion->accepted = false;
}

// Accept all suggestions
void smart_rename_accept_all(BatchRenameRequest *request)
{
    if (!request) return;
    for (int i = 0; i < request->count; i++) {
        request->suggestions[i].accepted = true;
    }
}

// Reject all suggestions
void smart_rename_reject_all(BatchRenameRequest *request)
{
    if (!request) return;
    for (int i = 0; i < request->count; i++) {
        request->suggestions[i].accepted = false;
    }
}

// Manually set suggestion name
void smart_rename_set_name(RenameSuggestion *suggestion, const char *name)
{
    if (!suggestion || !name) return;
    strncpy(suggestion->suggested_name, name, sizeof(suggestion->suggested_name) - 1);
    suggestion->confidence = 1.0f;
    strncpy(suggestion->reason, "User specified", sizeof(suggestion->reason) - 1);
}

// Get suggested name for a single file
SmartRenameStatus smart_rename_suggest_single(const char *path,
                                               const SmartRenameConfig *config,
                                               RenameSuggestion *suggestion)
{
    if (!path || !config || !suggestion) return RENAME_STATUS_NOT_INITIALIZED;

    BatchRenameRequest *request = smart_rename_request_create();
    if (!request) return RENAME_STATUS_NOT_INITIALIZED;

    if (!smart_rename_request_add_file(request, path)) {
        smart_rename_request_free(request);
        return RENAME_STATUS_FILE_ERROR;
    }

    SmartRenameStatus status = smart_rename_generate_suggestions(request, config);
    if (status == RENAME_STATUS_OK && request->count > 0) {
        *suggestion = request->suggestions[0];
    }

    smart_rename_request_free(request);
    return status;
}

// Learn from user's rename patterns (stores locally)
void smart_rename_learn(const char *original_name, const char *new_name)
{
    // In a full implementation, this would:
    // 1. Store patterns in a local SQLite database
    // 2. Analyze naming conventions (date formats, prefixes, etc.)
    // 3. Use these patterns to improve future suggestions
    // For now, this is a placeholder
    (void)original_name;
    (void)new_name;
}

// Get status message
const char *smart_rename_status_message(SmartRenameStatus status)
{
    switch (status) {
        case RENAME_STATUS_OK: return "OK";
        case RENAME_STATUS_NOT_INITIALIZED: return "Not initialized";
        case RENAME_STATUS_FILE_ERROR: return "File error";
        case RENAME_STATUS_API_ERROR: return "API error";
        case RENAME_STATUS_CANCELLED: return "Cancelled";
        case RENAME_STATUS_CONFLICT: return "Name conflict";
        default: return "Unknown";
    }
}

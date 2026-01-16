#include "summarize.h"
#include "../api/claude_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <sqlite3.h>
#include <CommonCrypto/CommonDigest.h>

// File extension to type mapping
typedef struct TypeMapping {
    const char *ext;
    SummaryFileType type;
} TypeMapping;

static const TypeMapping TYPE_MAP[] = {
    // Text
    {".txt", SUMM_TYPE_TEXT},
    {".rtf", SUMM_TYPE_TEXT},
    {".log", SUMM_TYPE_TEXT},

    // Code
    {".c", SUMM_TYPE_CODE},
    {".h", SUMM_TYPE_CODE},
    {".cpp", SUMM_TYPE_CODE},
    {".hpp", SUMM_TYPE_CODE},
    {".py", SUMM_TYPE_CODE},
    {".js", SUMM_TYPE_CODE},
    {".ts", SUMM_TYPE_CODE},
    {".java", SUMM_TYPE_CODE},
    {".go", SUMM_TYPE_CODE},
    {".rs", SUMM_TYPE_CODE},
    {".rb", SUMM_TYPE_CODE},
    {".php", SUMM_TYPE_CODE},
    {".swift", SUMM_TYPE_CODE},
    {".kt", SUMM_TYPE_CODE},
    {".cs", SUMM_TYPE_CODE},
    {".sh", SUMM_TYPE_CODE},
    {".bash", SUMM_TYPE_CODE},
    {".zsh", SUMM_TYPE_CODE},

    // Documents
    {".pdf", SUMM_TYPE_DOCUMENT},
    {".doc", SUMM_TYPE_DOCUMENT},
    {".docx", SUMM_TYPE_DOCUMENT},
    {".odt", SUMM_TYPE_DOCUMENT},

    // Markdown
    {".md", SUMM_TYPE_MARKDOWN},
    {".markdown", SUMM_TYPE_MARKDOWN},
    {".rst", SUMM_TYPE_MARKDOWN},

    // Data
    {".json", SUMM_TYPE_DATA},
    {".xml", SUMM_TYPE_DATA},
    {".yaml", SUMM_TYPE_DATA},
    {".yml", SUMM_TYPE_DATA},
    {".csv", SUMM_TYPE_DATA},
    {".toml", SUMM_TYPE_DATA},

    // Images
    {".jpg", SUMM_TYPE_IMAGE},
    {".jpeg", SUMM_TYPE_IMAGE},
    {".png", SUMM_TYPE_IMAGE},
    {".gif", SUMM_TYPE_IMAGE},
    {".webp", SUMM_TYPE_IMAGE},
    {".bmp", SUMM_TYPE_IMAGE},

    {NULL, SUMM_TYPE_UNKNOWN}
};

// Initialize default configuration
void summarize_config_init(SummarizeConfig *config)
{
    if (!config) return;

    memset(config, 0, sizeof(SummarizeConfig));
    config->default_level = SUMM_LEVEL_STANDARD;
    config->use_cache = true;
    strncpy(config->cache_path, "~/.cache/finder-plus/summaries.db",
            sizeof(config->cache_path) - 1);
    config->max_cache_age_days = 30;
    config->max_file_size = 10 * 1024 * 1024; // 10MB
    config->extract_key_points = true;
    config->include_metadata = false;
}

// Initialize summary cache
SummaryCache *summary_cache_create(const char *cache_path)
{
    SummaryCache *cache = calloc(1, sizeof(SummaryCache));
    if (!cache) return NULL;

    // Use default path if NULL
    const char *path = cache_path ? cache_path : "~/.cache/finder-plus/summaries.db";

    // Expand ~ in path
    char expanded_path[512];
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, path + 1);
        } else {
            strncpy(expanded_path, path, sizeof(expanded_path) - 1);
        }
    } else {
        strncpy(expanded_path, path, sizeof(expanded_path) - 1);
    }

    // Create directory if needed
    char *dir = strdup(expanded_path);
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        // Simple mkdir -p
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "mkdir -p '%s' 2>/dev/null", dir);
        system(cmd);
    }
    free(dir);

    // Open SQLite database
    sqlite3 *db;
    if (sqlite3_open(expanded_path, &db) != SQLITE_OK) {
        free(cache);
        return NULL;
    }

    // Create table
    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS summaries ("
        "  path TEXT PRIMARY KEY,"
        "  hash TEXT NOT NULL,"
        "  summary TEXT NOT NULL,"
        "  level INTEGER NOT NULL,"
        "  created INTEGER NOT NULL,"
        "  file_modified INTEGER NOT NULL,"
        "  file_size INTEGER NOT NULL"
        ");";

    char *err = NULL;
    if (sqlite3_exec(db, create_sql, NULL, NULL, &err) != SQLITE_OK) {
        sqlite3_free(err);
        sqlite3_close(db);
        free(cache);
        return NULL;
    }

    cache->db = db;
    cache->initialized = true;
    return cache;
}

// Close summary cache
void summary_cache_destroy(SummaryCache *cache)
{
    if (!cache) return;
    if (cache->db) {
        sqlite3_close(cache->db);
    }
    free(cache);
}

// Compute file hash for cache validation
static bool compute_file_hash(const char *path, char *hash_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);

    unsigned char buffer[8192];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        CC_SHA256_Update(&ctx, buffer, (CC_LONG)bytes);
    }

    fclose(f);

    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(hash, &ctx);

    // Convert to hex
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) {
        sprintf(hash_out + i * 2, "%02x", hash[i]);
    }
    hash_out[64] = '\0';

    return true;
}

// Get cached summary
bool summary_cache_get(SummaryCache *cache, const char *path, SummaryResult *result)
{
    if (!cache || !cache->initialized || !path || !result) return false;

    // Get file stats
    struct stat st;
    if (stat(path, &st) != 0) return false;

    // Query cache
    sqlite3 *db = cache->db;
    const char *sql = "SELECT hash, summary, level, created, file_modified, file_size "
                      "FROM summaries WHERE path = ?";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *cached_hash = (const char *)sqlite3_column_text(stmt, 0);
        time_t file_modified = sqlite3_column_int64(stmt, 4);
        uint64_t file_size = sqlite3_column_int64(stmt, 5);

        // Check if file has changed
        if (file_modified == st.st_mtime && file_size == (uint64_t)st.st_size) {
            // Verify hash for extra safety
            char current_hash[65];
            if (compute_file_hash(path, current_hash) &&
                strcmp(current_hash, cached_hash) == 0) {
                // Cache hit
                strncpy(result->path, path, sizeof(result->path) - 1);
                strncpy(result->summary, (const char *)sqlite3_column_text(stmt, 1),
                        sizeof(result->summary) - 1);
                result->level = sqlite3_column_int(stmt, 2);
                result->file_type = summarize_detect_file_type(path);
                result->from_cache = true;
                result->status = SUMM_STATUS_OK;
                found = true;
            }
        }
    }

    sqlite3_finalize(stmt);
    return found;
}

// Store summary in cache
bool summary_cache_put(SummaryCache *cache, const SummaryResult *result)
{
    if (!cache || !cache->initialized || !result) return false;

    // Get file stats
    struct stat st;
    if (stat(result->path, &st) != 0) return false;

    // Compute hash
    char hash[65];
    if (!compute_file_hash(result->path, hash)) return false;

    sqlite3 *db = cache->db;
    const char *sql = "INSERT OR REPLACE INTO summaries "
                      "(path, hash, summary, level, created, file_modified, file_size) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, result->path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, result->summary, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, result->level);
    sqlite3_bind_int64(stmt, 5, time(NULL));
    sqlite3_bind_int64(stmt, 6, st.st_mtime);
    sqlite3_bind_int64(stmt, 7, st.st_size);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    return success;
}

// Invalidate cache entry
void summary_cache_invalidate(SummaryCache *cache, const char *path)
{
    if (!cache || !cache->initialized || !path) return;

    sqlite3 *db = cache->db;
    const char *sql = "DELETE FROM summaries WHERE path = ?";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// Clear entire cache
void summary_cache_clear(SummaryCache *cache)
{
    if (!cache || !cache->initialized) return;

    sqlite3 *db = cache->db;
    sqlite3_exec(db, "DELETE FROM summaries", NULL, NULL, NULL);
}

// Get file type for summarization
SummaryFileType summarize_detect_file_type(const char *path)
{
    if (!path) return SUMM_TYPE_UNKNOWN;

    const char *ext = strrchr(path, '.');
    if (!ext) return SUMM_TYPE_UNKNOWN;

    for (int i = 0; TYPE_MAP[i].ext; i++) {
        if (strcasecmp(ext, TYPE_MAP[i].ext) == 0) {
            return TYPE_MAP[i].type;
        }
    }

    return SUMM_TYPE_UNKNOWN;
}

// Check if file type is supported
bool summarize_is_supported(const char *path)
{
    SummaryFileType type = summarize_detect_file_type(path);
    return type != SUMM_TYPE_UNKNOWN && type != SUMM_TYPE_IMAGE;
}

// Read file content for summarization
static bool read_file_content(const char *path, char *content, size_t max_size, size_t *actual_size)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;

    size_t len = fread(content, 1, max_size - 1, f);
    content[len] = '\0';
    if (actual_size) *actual_size = len;

    fclose(f);
    return len > 0;
}

// Build summarization prompt
static void build_summary_prompt(const char *content, SummaryFileType type, SummaryLevel level,
                                  bool extract_key_points, char *prompt, size_t prompt_size)
{
    const char *type_context = "";
    switch (type) {
        case SUMM_TYPE_CODE:
            type_context = "This is source code. Focus on what the code does, key functions, and its purpose.";
            break;
        case SUMM_TYPE_DOCUMENT:
            type_context = "This is a document. Focus on the main topic, key points, and conclusions.";
            break;
        case SUMM_TYPE_MARKDOWN:
            type_context = "This is markdown content. Focus on the main topic and key sections.";
            break;
        case SUMM_TYPE_DATA:
            type_context = "This is structured data. Focus on what data it contains and its structure.";
            break;
        default:
            type_context = "Focus on the main content and key information.";
            break;
    }

    const char *length_instruction = "";
    switch (level) {
        case SUMM_LEVEL_BRIEF:
            length_instruction = "Provide a 1-2 sentence summary.";
            break;
        case SUMM_LEVEL_STANDARD:
            length_instruction = "Provide a concise paragraph summary.";
            break;
        case SUMM_LEVEL_DETAILED:
            length_instruction = "Provide a detailed summary with multiple paragraphs.";
            break;
    }

    const char *key_points_instruction = "";
    if (extract_key_points) {
        key_points_instruction = "\n\nAlso list 3-5 key points as bullet points.";
    }

    snprintf(prompt, prompt_size,
             "%s\n\n%s%s\n\nContent to summarize:\n\n%s",
             type_context, length_instruction, key_points_instruction, content);
}

// Summarize a file
SummarizeStatus summarize_file(const char *path,
                                const SummarizeConfig *config,
                                SummaryCache *cache,
                                SummaryResult *result)
{
    if (!path || !config || !result) {
        return SUMM_STATUS_NOT_INITIALIZED;
    }

    memset(result, 0, sizeof(SummaryResult));
    strncpy(result->path, path, sizeof(result->path) - 1);

    // Check file exists
    struct stat st;
    if (stat(path, &st) != 0) {
        result->status = SUMM_STATUS_FILE_ERROR;
        strncpy(result->error_message, "File not found", sizeof(result->error_message) - 1);
        return SUMM_STATUS_FILE_ERROR;
    }

    // Check file size
    if (st.st_size > config->max_file_size) {
        result->status = SUMM_STATUS_TOO_LARGE;
        strncpy(result->error_message, "File too large", sizeof(result->error_message) - 1);
        return SUMM_STATUS_TOO_LARGE;
    }

    // Detect file type
    result->file_type = summarize_detect_file_type(path);
    if (result->file_type == SUMM_TYPE_UNKNOWN) {
        result->status = SUMM_STATUS_UNSUPPORTED_TYPE;
        strncpy(result->error_message, "Unsupported file type", sizeof(result->error_message) - 1);
        return SUMM_STATUS_UNSUPPORTED_TYPE;
    }

    // Image summarization not supported via text API
    if (result->file_type == SUMM_TYPE_IMAGE) {
        result->status = SUMM_STATUS_UNSUPPORTED_TYPE;
        strncpy(result->error_message, "Image summarization requires vision model",
                sizeof(result->error_message) - 1);
        return SUMM_STATUS_UNSUPPORTED_TYPE;
    }

    // Check cache
    if (config->use_cache && cache) {
        if (summary_cache_get(cache, path, result)) {
            return SUMM_STATUS_OK;
        }
    }

    // Check API key
    if (strlen(config->api_key) == 0) {
        result->status = SUMM_STATUS_API_ERROR;
        strncpy(result->error_message, "No API key configured", sizeof(result->error_message) - 1);
        return SUMM_STATUS_API_ERROR;
    }

    // Read file content
    char *content = malloc(SUMMARY_MAX_CONTENT);
    if (!content) {
        result->status = SUMM_STATUS_FILE_ERROR;
        return SUMM_STATUS_FILE_ERROR;
    }

    size_t content_len;
    if (!read_file_content(path, content, SUMMARY_MAX_CONTENT, &content_len)) {
        free(content);
        result->status = SUMM_STATUS_FILE_ERROR;
        strncpy(result->error_message, "Could not read file", sizeof(result->error_message) - 1);
        return SUMM_STATUS_FILE_ERROR;
    }

    // Build prompt
    char *prompt = malloc(SUMMARY_MAX_CONTENT + 1024);
    if (!prompt) {
        free(content);
        result->status = SUMM_STATUS_FILE_ERROR;
        return SUMM_STATUS_FILE_ERROR;
    }

    build_summary_prompt(content, result->file_type, config->default_level,
                         config->extract_key_points, prompt, SUMMARY_MAX_CONTENT + 1024);

    free(content);

    // Send to Claude
    clock_t start = clock();

    ClaudeClient *client = claude_client_create(config->api_key);
    if (!client) {
        free(prompt);
        result->status = SUMM_STATUS_API_ERROR;
        strncpy(result->error_message, "Failed to create API client",
                sizeof(result->error_message) - 1);
        return SUMM_STATUS_API_ERROR;
    }

    ClaudeMessageRequest req;
    ClaudeMessageResponse resp;
    claude_request_init(&req);
    claude_response_init(&resp);

    claude_request_set_system_prompt(&req, "You are a helpful assistant that summarizes documents concisely and accurately.");
    claude_request_add_user_message(&req, prompt);

    free(prompt);

    bool success = claude_send_message(client, &req, &resp);

    clock_t end = clock();
    result->generation_time_ms = (float)(end - start) / CLOCKS_PER_SEC * 1000.0f;

    if (success && resp.stop_reason != CLAUDE_STOP_ERROR) {
        strncpy(result->summary, resp.content, sizeof(result->summary) - 1);
        result->level = config->default_level;
        result->from_cache = false;
        result->tokens_used = resp.input_tokens + resp.output_tokens;
        result->status = SUMM_STATUS_OK;

        // Cache result
        if (config->use_cache && cache) {
            summary_cache_put(cache, result);
        }
    } else {
        result->status = SUMM_STATUS_API_ERROR;
        if (resp.error) {
            strncpy(result->error_message, resp.error, sizeof(result->error_message) - 1);
        } else {
            strncpy(result->error_message, "API request failed", sizeof(result->error_message) - 1);
        }
    }

    claude_request_cleanup(&req);
    claude_response_cleanup(&resp);
    claude_client_destroy(client);

    return result->status;
}

// Summarize multiple files
SummarizeStatus summarize_files(const char **paths,
                                 int count,
                                 const SummarizeConfig *config,
                                 SummaryCache *cache,
                                 SummaryResult *results)
{
    if (!paths || count <= 0 || !config || !results) {
        return SUMM_STATUS_NOT_INITIALIZED;
    }

    SummarizeStatus overall_status = SUMM_STATUS_OK;

    for (int i = 0; i < count; i++) {
        SummarizeStatus status = summarize_file(paths[i], config, cache, &results[i]);
        if (status != SUMM_STATUS_OK) {
            overall_status = status;
        }
    }

    return overall_status;
}

// Force re-summarize
SummarizeStatus summarize_file_force(const char *path,
                                      const SummarizeConfig *config,
                                      SummaryCache *cache,
                                      SummaryResult *result)
{
    // Invalidate cache first
    if (cache) {
        summary_cache_invalidate(cache, path);
    }

    return summarize_file(path, config, cache, result);
}

// Quick summary
SummarizeStatus summarize_quick(const char *path,
                                 const SummarizeConfig *config,
                                 SummaryCache *cache,
                                 char *summary_out,
                                 size_t summary_size)
{
    SummarizeConfig quick_config = *config;
    quick_config.default_level = SUMM_LEVEL_BRIEF;
    quick_config.extract_key_points = false;

    SummaryResult result;
    SummarizeStatus status = summarize_file(path, &quick_config, cache, &result);

    if (status == SUMM_STATUS_OK && summary_out && summary_size > 0) {
        strncpy(summary_out, result.summary, summary_size - 1);
        summary_out[summary_size - 1] = '\0';
    }

    return status;
}

// Summarize text content directly
SummarizeStatus summarize_text(const char *content,
                                SummaryFileType type,
                                const SummarizeConfig *config,
                                SummaryResult *result)
{
    if (!content || !config || !result) {
        return SUMM_STATUS_NOT_INITIALIZED;
    }

    memset(result, 0, sizeof(SummaryResult));
    result->file_type = type;

    if (strlen(config->api_key) == 0) {
        result->status = SUMM_STATUS_API_ERROR;
        return SUMM_STATUS_API_ERROR;
    }

    // Build prompt
    char *prompt = malloc(SUMMARY_MAX_CONTENT + 1024);
    if (!prompt) {
        result->status = SUMM_STATUS_FILE_ERROR;
        return SUMM_STATUS_FILE_ERROR;
    }

    build_summary_prompt(content, type, config->default_level,
                         config->extract_key_points, prompt, SUMMARY_MAX_CONTENT + 1024);

    // Send to Claude
    ClaudeClient *client = claude_client_create(config->api_key);
    if (!client) {
        free(prompt);
        result->status = SUMM_STATUS_API_ERROR;
        return SUMM_STATUS_API_ERROR;
    }

    ClaudeMessageRequest req;
    ClaudeMessageResponse resp;
    claude_request_init(&req);
    claude_response_init(&resp);

    claude_request_set_system_prompt(&req, "You are a helpful assistant that summarizes content.");
    claude_request_add_user_message(&req, prompt);

    free(prompt);

    bool success = claude_send_message(client, &req, &resp);

    if (success && resp.stop_reason != CLAUDE_STOP_ERROR) {
        strncpy(result->summary, resp.content, sizeof(result->summary) - 1);
        result->status = SUMM_STATUS_OK;
    } else {
        result->status = SUMM_STATUS_API_ERROR;
    }

    claude_request_cleanup(&req);
    claude_response_cleanup(&resp);
    claude_client_destroy(client);

    return result->status;
}

// Extract key points
SummarizeStatus summarize_extract_key_points(const char *path,
                                              const SummarizeConfig *config,
                                              char *points_out,
                                              size_t points_size,
                                              int max_points)
{
    if (!path || !config || !points_out || points_size == 0) {
        return SUMM_STATUS_NOT_INITIALIZED;
    }

    // Read file
    char *content = malloc(SUMMARY_MAX_CONTENT);
    if (!content) return SUMM_STATUS_FILE_ERROR;

    if (!read_file_content(path, content, SUMMARY_MAX_CONTENT, NULL)) {
        free(content);
        return SUMM_STATUS_FILE_ERROR;
    }

    // Build prompt
    char prompt[SUMMARY_MAX_CONTENT + 512];
    snprintf(prompt, sizeof(prompt),
             "Extract exactly %d key points from this content. "
             "Format as a numbered list.\n\n%s",
             max_points, content);

    free(content);

    // Send to Claude
    ClaudeClient *client = claude_client_create(config->api_key);
    if (!client) return SUMM_STATUS_API_ERROR;

    ClaudeMessageRequest req;
    ClaudeMessageResponse resp;
    claude_request_init(&req);
    claude_response_init(&resp);

    claude_request_add_user_message(&req, prompt);

    bool success = claude_send_message(client, &req, &resp);

    if (success && resp.stop_reason != CLAUDE_STOP_ERROR) {
        strncpy(points_out, resp.content, points_size - 1);
        points_out[points_size - 1] = '\0';
    }

    claude_request_cleanup(&req);
    claude_response_cleanup(&resp);
    claude_client_destroy(client);

    return success ? SUMM_STATUS_OK : SUMM_STATUS_API_ERROR;
}

// Compare two documents
SummarizeStatus summarize_compare(const char *path1,
                                   const char *path2,
                                   const SummarizeConfig *config,
                                   char *comparison_out,
                                   size_t comparison_size)
{
    if (!path1 || !path2 || !config || !comparison_out || comparison_size == 0) {
        return SUMM_STATUS_NOT_INITIALIZED;
    }

    // Read both files
    char *content1 = malloc(SUMMARY_MAX_CONTENT / 2);
    char *content2 = malloc(SUMMARY_MAX_CONTENT / 2);
    if (!content1 || !content2) {
        free(content1);
        free(content2);
        return SUMM_STATUS_FILE_ERROR;
    }

    if (!read_file_content(path1, content1, SUMMARY_MAX_CONTENT / 2, NULL) ||
        !read_file_content(path2, content2, SUMMARY_MAX_CONTENT / 2, NULL)) {
        free(content1);
        free(content2);
        return SUMM_STATUS_FILE_ERROR;
    }

    // Build prompt
    char *prompt = malloc(SUMMARY_MAX_CONTENT + 1024);
    if (!prompt) {
        free(content1);
        free(content2);
        return SUMM_STATUS_FILE_ERROR;
    }

    snprintf(prompt, SUMMARY_MAX_CONTENT + 1024,
             "Compare these two documents and summarize the key differences.\n\n"
             "Document 1:\n%s\n\n"
             "Document 2:\n%s",
             content1, content2);

    free(content1);
    free(content2);

    // Send to Claude
    ClaudeClient *client = claude_client_create(config->api_key);
    if (!client) {
        free(prompt);
        return SUMM_STATUS_API_ERROR;
    }

    ClaudeMessageRequest req;
    ClaudeMessageResponse resp;
    claude_request_init(&req);
    claude_response_init(&resp);

    claude_request_add_user_message(&req, prompt);

    free(prompt);

    bool success = claude_send_message(client, &req, &resp);

    if (success && resp.stop_reason != CLAUDE_STOP_ERROR) {
        strncpy(comparison_out, resp.content, comparison_size - 1);
        comparison_out[comparison_size - 1] = '\0';
    }

    claude_request_cleanup(&req);
    claude_response_cleanup(&resp);
    claude_client_destroy(client);

    return success ? SUMM_STATUS_OK : SUMM_STATUS_API_ERROR;
}

// Get status message
const char *summarize_status_message(SummarizeStatus status)
{
    switch (status) {
        case SUMM_STATUS_OK: return "OK";
        case SUMM_STATUS_NOT_INITIALIZED: return "Not initialized";
        case SUMM_STATUS_FILE_ERROR: return "File error";
        case SUMM_STATUS_UNSUPPORTED_TYPE: return "Unsupported file type";
        case SUMM_STATUS_API_ERROR: return "API error";
        case SUMM_STATUS_CACHE_ERROR: return "Cache error";
        case SUMM_STATUS_TOO_LARGE: return "File too large";
        default: return "Unknown";
    }
}

// Get file type name
const char *summarize_file_type_name(SummaryFileType type)
{
    switch (type) {
        case SUMM_TYPE_TEXT: return "Text";
        case SUMM_TYPE_CODE: return "Code";
        case SUMM_TYPE_DOCUMENT: return "Document";
        case SUMM_TYPE_MARKDOWN: return "Markdown";
        case SUMM_TYPE_DATA: return "Data";
        case SUMM_TYPE_IMAGE: return "Image";
        default: return "Unknown";
    }
}

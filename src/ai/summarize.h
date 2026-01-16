#ifndef SUMMARIZE_H
#define SUMMARIZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Maximum summary length
#define SUMMARY_MAX_LENGTH 2048

// Maximum content to send to API (512KB)
#define SUMMARY_MAX_CONTENT 524288

// Summarization status
typedef enum SummarizeStatus {
    SUMM_STATUS_OK = 0,
    SUMM_STATUS_NOT_INITIALIZED,
    SUMM_STATUS_FILE_ERROR,
    SUMM_STATUS_UNSUPPORTED_TYPE,
    SUMM_STATUS_API_ERROR,
    SUMM_STATUS_CACHE_ERROR,
    SUMM_STATUS_TOO_LARGE
} SummarizeStatus;

// Summary detail level
typedef enum SummaryLevel {
    SUMM_LEVEL_BRIEF,       // 1-2 sentences
    SUMM_LEVEL_STANDARD,    // 1 paragraph
    SUMM_LEVEL_DETAILED     // Multiple paragraphs
} SummaryLevel;

// File type for summary context
typedef enum SummaryFileType {
    SUMM_TYPE_TEXT,         // Plain text
    SUMM_TYPE_CODE,         // Source code
    SUMM_TYPE_DOCUMENT,     // PDF, DOC, etc.
    SUMM_TYPE_MARKDOWN,     // Markdown
    SUMM_TYPE_DATA,         // JSON, XML, CSV
    SUMM_TYPE_IMAGE,        // Image (via CLIP description)
    SUMM_TYPE_UNKNOWN
} SummaryFileType;

// Cached summary entry
typedef struct SummaryCacheEntry {
    char path[1024];
    char hash[65];              // SHA256 hash of file for cache invalidation
    char summary[SUMMARY_MAX_LENGTH];
    SummaryLevel level;
    time_t created;
    time_t file_modified;       // File mtime when summary was created
    uint64_t file_size;
} SummaryCacheEntry;

// Summary result
typedef struct SummaryResult {
    char path[1024];
    char summary[SUMMARY_MAX_LENGTH];
    SummaryFileType file_type;
    SummaryLevel level;
    bool from_cache;
    float generation_time_ms;
    int tokens_used;
    SummarizeStatus status;
    char error_message[256];
} SummaryResult;

// Summary configuration
typedef struct SummarizeConfig {
    char api_key[256];          // Claude API key
    SummaryLevel default_level;
    bool use_cache;             // Cache summaries locally
    char cache_path[256];       // Path to cache database
    int max_cache_age_days;     // Invalidate cache entries older than this
    int max_file_size;          // Max file size to summarize (bytes)
    bool extract_key_points;    // Include bullet points
    bool include_metadata;      // Include file metadata in summary
} SummarizeConfig;

// Summary cache
typedef struct SummaryCache {
    void *db;                   // SQLite database handle
    bool initialized;
} SummaryCache;

// Initialize default configuration
void summarize_config_init(SummarizeConfig *config);

// Initialize summary cache
SummaryCache *summary_cache_create(const char *cache_path);

// Close summary cache
void summary_cache_destroy(SummaryCache *cache);

// Get cached summary (if exists and valid)
bool summary_cache_get(SummaryCache *cache, const char *path, SummaryResult *result);

// Store summary in cache
bool summary_cache_put(SummaryCache *cache, const SummaryResult *result);

// Invalidate cache entry
void summary_cache_invalidate(SummaryCache *cache, const char *path);

// Clear entire cache
void summary_cache_clear(SummaryCache *cache);

// Get file type for summarization
SummaryFileType summarize_detect_file_type(const char *path);

// Check if file type is supported
bool summarize_is_supported(const char *path);

// Summarize a file (uses cache if available)
SummarizeStatus summarize_file(const char *path,
                                const SummarizeConfig *config,
                                SummaryCache *cache,
                                SummaryResult *result);

// Summarize multiple files
SummarizeStatus summarize_files(const char **paths,
                                 int count,
                                 const SummarizeConfig *config,
                                 SummaryCache *cache,
                                 SummaryResult *results);

// Force re-summarize (bypass cache)
SummarizeStatus summarize_file_force(const char *path,
                                      const SummarizeConfig *config,
                                      SummaryCache *cache,
                                      SummaryResult *result);

// Get quick summary (brief level, prefer cache)
SummarizeStatus summarize_quick(const char *path,
                                 const SummarizeConfig *config,
                                 SummaryCache *cache,
                                 char *summary_out,
                                 size_t summary_size);

// Summarize text content directly (no file)
SummarizeStatus summarize_text(const char *content,
                                SummaryFileType type,
                                const SummarizeConfig *config,
                                SummaryResult *result);

// Extract key points from document
SummarizeStatus summarize_extract_key_points(const char *path,
                                              const SummarizeConfig *config,
                                              char *points_out,
                                              size_t points_size,
                                              int max_points);

// Compare two documents and summarize differences
SummarizeStatus summarize_compare(const char *path1,
                                   const char *path2,
                                   const SummarizeConfig *config,
                                   char *comparison_out,
                                   size_t comparison_size);

// Get status message
const char *summarize_status_message(SummarizeStatus status);

// Get file type name
const char *summarize_file_type_name(SummaryFileType type);

#endif // SUMMARIZE_H

#ifndef SMART_RENAME_H
#define SMART_RENAME_H

#include <stdbool.h>
#include <stddef.h>

// Maximum number of files for batch rename
#define RENAME_MAX_FILES 100

// Maximum length of suggested name
#define RENAME_MAX_NAME_LEN 256

// Name format styles
typedef enum RenameFormat {
    RENAME_FORMAT_ORIGINAL,     // Keep original format
    RENAME_FORMAT_SNAKE_CASE,   // snake_case
    RENAME_FORMAT_KEBAB_CASE,   // kebab-case
    RENAME_FORMAT_CAMEL_CASE,   // camelCase
    RENAME_FORMAT_PASCAL_CASE,  // PascalCase
    RENAME_FORMAT_TITLE_CASE    // Title Case
} RenameFormat;

// Smart rename status
typedef enum SmartRenameStatus {
    RENAME_STATUS_OK = 0,
    RENAME_STATUS_NOT_INITIALIZED,
    RENAME_STATUS_FILE_ERROR,
    RENAME_STATUS_API_ERROR,
    RENAME_STATUS_CANCELLED,
    RENAME_STATUS_CONFLICT
} SmartRenameStatus;

// Rename suggestion for a single file
typedef struct RenameSuggestion {
    char original_path[1024];       // Original file path
    char original_name[RENAME_MAX_NAME_LEN];  // Original filename
    char suggested_name[RENAME_MAX_NAME_LEN]; // AI-suggested name
    char extension[32];             // File extension (preserved)
    char reason[256];               // Why this name was suggested
    float confidence;               // 0.0 - 1.0 confidence score
    bool accepted;                  // User accepted this suggestion
    bool has_conflict;              // Would conflict with existing file
    SmartRenameStatus status;
} RenameSuggestion;

// Batch rename request
typedef struct BatchRenameRequest {
    RenameSuggestion *suggestions;
    int count;
    int capacity;
    RenameFormat format;            // Desired name format
    char pattern[256];              // Optional pattern (e.g., "photo_%date%_%index%")
    bool preserve_extension;        // Keep original extension
    bool use_content;               // Use file content for context
    bool use_metadata;              // Use file metadata for context
} BatchRenameRequest;

// Batch rename result
typedef struct BatchRenameResult {
    int total_files;
    int renamed_count;
    int skipped_count;
    int error_count;
    char error_message[512];
    SmartRenameStatus status;
} BatchRenameResult;

// Smart rename configuration
typedef struct SmartRenameConfig {
    char api_key[256];              // Claude API key
    RenameFormat default_format;
    bool auto_detect_format;        // Detect format from existing files
    int max_content_bytes;          // Max bytes to read for content context
    int max_concurrent;             // Max concurrent API requests
    float min_confidence;           // Minimum confidence to suggest (0.0 - 1.0)
} SmartRenameConfig;

// Initialize configuration with defaults
void smart_rename_config_init(SmartRenameConfig *config);

// Create batch rename request
BatchRenameRequest *smart_rename_request_create(void);

// Free batch rename request
void smart_rename_request_free(BatchRenameRequest *request);

// Add file to batch rename request
bool smart_rename_request_add_file(BatchRenameRequest *request, const char *path);

// Generate AI suggestions for files (uses Claude API)
SmartRenameStatus smart_rename_generate_suggestions(BatchRenameRequest *request,
                                                     const SmartRenameConfig *config);

// Generate suggestions using a pattern (no AI needed)
SmartRenameStatus smart_rename_apply_pattern(BatchRenameRequest *request,
                                              const char *pattern);

// Convert name to specified format
void smart_rename_format_name(const char *name, RenameFormat format, char *output, size_t output_size);

// Preview rename operation (check for conflicts)
bool smart_rename_preview(BatchRenameRequest *request);

// Execute rename operation
BatchRenameResult smart_rename_execute(BatchRenameRequest *request);

// Undo last batch rename (if within session)
bool smart_rename_undo(void);

// Accept/reject individual suggestions
void smart_rename_accept(RenameSuggestion *suggestion);
void smart_rename_reject(RenameSuggestion *suggestion);
void smart_rename_accept_all(BatchRenameRequest *request);
void smart_rename_reject_all(BatchRenameRequest *request);

// Manually set suggestion name
void smart_rename_set_name(RenameSuggestion *suggestion, const char *name);

// Get suggested name for a single file (synchronous)
SmartRenameStatus smart_rename_suggest_single(const char *path,
                                               const SmartRenameConfig *config,
                                               RenameSuggestion *suggestion);

// Learn from user's rename patterns (stores locally)
void smart_rename_learn(const char *original_name, const char *new_name);

// Get status message
const char *smart_rename_status_message(SmartRenameStatus status);

// Pattern replacement helpers
// Supported patterns: %name%, %date%, %time%, %index%, %ext%, %type%, %size%
void smart_rename_expand_pattern(const char *pattern, const char *path, int index,
                                  char *output, size_t output_size);

#endif // SMART_RENAME_H

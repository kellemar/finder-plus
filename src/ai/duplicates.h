#ifndef DUPLICATES_H
#define DUPLICATES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Maximum number of files to analyze in one batch
#define DUPLICATES_MAX_FILES 10000

// Maximum number of duplicate groups
#define DUPLICATES_MAX_GROUPS 1000

// Hash size in bytes
#define HASH_SIZE_MD5 16
#define HASH_SIZE_SHA256 32
#define HASH_SIZE_PERCEPTUAL 8

// Duplicate detection status
typedef enum DuplicateStatus {
    DUP_STATUS_OK = 0,
    DUP_STATUS_NOT_INITIALIZED,
    DUP_STATUS_FILE_ERROR,
    DUP_STATUS_MEMORY_ERROR,
    DUP_STATUS_CANCELLED,
    DUP_STATUS_TOO_MANY_FILES
} DuplicateStatus;

// Duplicate type
typedef enum DuplicateType {
    DUP_TYPE_EXACT,         // Identical file content (same hash)
    DUP_TYPE_SIMILAR_IMAGE, // Perceptually similar images
    DUP_TYPE_SIMILAR_TEXT   // Semantically similar text files
} DuplicateType;

// Keep suggestion reason
typedef enum KeepReason {
    KEEP_NEWEST,            // Most recently modified
    KEEP_OLDEST,            // Original/oldest version
    KEEP_LARGEST,           // Highest quality/resolution
    KEEP_SHORTEST_PATH,     // Simplest location
    KEEP_MOST_ACCESSED,     // Most frequently used
    KEEP_USER_CHOICE        // User specified
} KeepReason;

// File info for duplicate detection
typedef struct DuplicateFileInfo {
    char path[1024];
    uint64_t size;
    time_t modified;
    time_t accessed;
    uint8_t hash_md5[HASH_SIZE_MD5];
    uint8_t hash_perceptual[HASH_SIZE_PERCEPTUAL];
    float similarity_score;     // 0.0 - 1.0
    bool is_suggested_keep;
    KeepReason keep_reason;
} DuplicateFileInfo;

// Group of duplicate files
typedef struct DuplicateGroup {
    DuplicateType type;
    DuplicateFileInfo *files;
    int file_count;
    int capacity;
    uint64_t total_size;        // Total size of all duplicates
    uint64_t reclaimable_size;  // Size that can be freed
    int suggested_keep_index;   // Index of file to keep
    KeepReason keep_reason;
} DuplicateGroup;

// Duplicate analysis result
typedef struct DuplicateAnalysis {
    DuplicateGroup *groups;
    int group_count;
    int group_capacity;

    // Statistics
    int total_files_scanned;
    int total_duplicates_found;
    uint64_t total_reclaimable_size;
    float scan_time_ms;

    DuplicateStatus status;
} DuplicateAnalysis;

// Duplicate detector configuration
typedef struct DuplicateConfig {
    bool detect_exact;          // Detect exact duplicates (default: true)
    bool detect_similar_images; // Detect similar images (default: true)
    bool detect_similar_text;   // Detect similar text files (default: true)
    float similarity_threshold; // 0.0 - 1.0, default 0.9 for images, 0.85 for text
    int min_file_size;          // Minimum file size to consider (default: 1 byte)
    int max_file_size;          // Maximum file size to scan (default: 1GB)
    bool recursive;             // Scan subdirectories (default: true)
    const char **exclude_patterns;  // Patterns to exclude
    int exclude_count;
    bool cancelled;             // Set to true to cancel operation
} DuplicateConfig;

// Progress callback
typedef void (*DuplicateProgressCallback)(int files_scanned, int total_files, const char *current_file, void *user_data);

// Initialize default configuration
void duplicate_config_init(DuplicateConfig *config);

// Create analysis result
DuplicateAnalysis *duplicate_analysis_create(void);

// Free analysis result
void duplicate_analysis_free(DuplicateAnalysis *analysis);

// Scan directory for duplicates
DuplicateStatus duplicate_scan_directory(const char *path,
                                          const DuplicateConfig *config,
                                          DuplicateProgressCallback progress,
                                          void *user_data,
                                          DuplicateAnalysis *result);

// Scan multiple directories
DuplicateStatus duplicate_scan_directories(const char **paths,
                                            int path_count,
                                            const DuplicateConfig *config,
                                            DuplicateProgressCallback progress,
                                            void *user_data,
                                            DuplicateAnalysis *result);

// Find duplicates of a specific file
DuplicateStatus duplicate_find_copies(const char *file_path,
                                       const char *search_dir,
                                       const DuplicateConfig *config,
                                       DuplicateAnalysis *result);

// Hash functions
bool hash_file_md5(const char *path, uint8_t *hash_out);
bool hash_file_sha256(const char *path, uint8_t *hash_out);

// Perceptual hash for images (average hash algorithm)
bool hash_image_perceptual(const char *path, uint8_t *hash_out);

// Calculate Hamming distance between perceptual hashes
int hash_hamming_distance(const uint8_t *hash1, const uint8_t *hash2, size_t len);

// Compare two files for exact match (byte-by-byte)
bool files_are_identical(const char *path1, const char *path2);

// Suggest which file to keep in a group
void duplicate_suggest_keep(DuplicateGroup *group, KeepReason preference);

// Delete all duplicates except suggested keep (moves to trash)
int duplicate_cleanup_group(DuplicateGroup *group, bool use_trash);

// Export analysis to JSON
char *duplicate_analysis_to_json(const DuplicateAnalysis *analysis);

// Get status message
const char *duplicate_status_message(DuplicateStatus status);

// Hash to hex string
void hash_to_hex(const uint8_t *hash, size_t len, char *hex_out);

#endif // DUPLICATES_H

#ifndef ORGANIZATION_H
#define ORGANIZATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Maximum number of categories
#define ORG_MAX_CATEGORIES 32

// Maximum files per category
#define ORG_MAX_FILES_PER_CATEGORY 1000

// Maximum suggested folders
#define ORG_MAX_FOLDERS 64

// Organization status
typedef enum OrganizationStatus {
    ORG_STATUS_OK = 0,
    ORG_STATUS_NOT_INITIALIZED,
    ORG_STATUS_FILE_ERROR,
    ORG_STATUS_API_ERROR,
    ORG_STATUS_MEMORY_ERROR,
    ORG_STATUS_CANCELLED
} OrganizationStatus;

// File category types
typedef enum FileCategory {
    CAT_DOCUMENTS,      // PDFs, DOCs, text files
    CAT_IMAGES,         // Photos, screenshots, graphics
    CAT_VIDEOS,         // Movies, clips
    CAT_AUDIO,          // Music, recordings
    CAT_ARCHIVES,       // ZIP, TAR, RAR
    CAT_CODE,           // Source code files
    CAT_DATA,           // JSON, CSV, XML
    CAT_APPLICATIONS,   // DMG, PKG, executables
    CAT_FONTS,          // TTF, OTF
    CAT_OTHER           // Uncategorized
} FileCategory;

// Subcategory for more specific organization
typedef struct SubCategory {
    char name[64];              // e.g., "Invoices", "Screenshots"
    char folder_name[64];       // Suggested folder name
    int file_count;
    uint64_t total_size;
} SubCategory;

// Organized file entry
typedef struct OrganizedFile {
    char path[1024];
    char name[256];
    FileCategory category;
    int subcategory_index;      // Index into subcategories array
    uint64_t size;
    char suggested_folder[256]; // Where to move this file
    bool should_move;           // User confirmed move
    bool is_duplicate;          // Marked as duplicate
    bool is_old;                // Older than threshold
} OrganizedFile;

// Category statistics
typedef struct CategoryStats {
    FileCategory category;
    char name[32];
    int file_count;
    uint64_t total_size;
    SubCategory subcategories[16];
    int subcategory_count;
} CategoryStats;

// Suggested folder structure
typedef struct SuggestedFolder {
    char path[256];             // Relative path from root
    char description[128];      // Why this folder is suggested
    int file_count;             // Files that would go here
    uint64_t total_size;
    bool exists;                // Already exists
    bool selected;              // User selected to create
} SuggestedFolder;

// Organization analysis result
typedef struct OrganizationAnalysis {
    // Source directory
    char source_path[1024];

    // All files analyzed
    OrganizedFile *files;
    int file_count;
    int file_capacity;

    // Category breakdown
    CategoryStats categories[ORG_MAX_CATEGORIES];
    int category_count;

    // Suggested folder structure
    SuggestedFolder suggested_folders[ORG_MAX_FOLDERS];
    int folder_count;

    // Summary statistics
    uint64_t total_size;
    int total_files;
    int duplicate_count;
    uint64_t duplicate_size;
    int old_file_count;         // Files older than threshold
    uint64_t old_file_size;

    // Cleanup suggestions
    uint64_t suggested_cleanup_size;
    int suggested_cleanup_count;

    // Timing
    float analysis_time_ms;

    OrganizationStatus status;
} OrganizationAnalysis;

// Organization configuration
typedef struct OrganizationConfig {
    bool analyze_content;       // Use AI to analyze file content
    bool detect_duplicates;     // Mark duplicates
    int old_file_days;          // Files older than this are "old" (default: 365)
    bool recursive;             // Scan subdirectories
    const char **exclude_patterns;
    int exclude_count;
    char api_key[256];          // For AI-powered categorization
    bool create_subfolders;     // Create detailed subfolder structure
} OrganizationConfig;

// Progress callback
typedef void (*OrganizationProgressCallback)(int files_analyzed, int total_files, const char *current_file, void *user_data);

// Initialize default configuration
void organization_config_init(OrganizationConfig *config);

// Create analysis result
OrganizationAnalysis *organization_analysis_create(void);

// Free analysis result
void organization_analysis_free(OrganizationAnalysis *analysis);

// Analyze directory and generate organization suggestions
OrganizationStatus organization_analyze(const char *path,
                                         const OrganizationConfig *config,
                                         OrganizationProgressCallback progress,
                                         void *user_data,
                                         OrganizationAnalysis *result);

// Generate AI-powered organization suggestions (uses Claude)
OrganizationStatus organization_ai_suggest(OrganizationAnalysis *analysis,
                                            const OrganizationConfig *config);

// Preview organization plan (shows what would change)
char *organization_preview_plan(const OrganizationAnalysis *analysis);

// Execute organization plan
typedef struct OrganizationResult {
    int files_moved;
    int folders_created;
    int errors;
    uint64_t bytes_moved;
    char error_message[512];
    OrganizationStatus status;
} OrganizationResult;

OrganizationResult organization_execute(OrganizationAnalysis *analysis, bool use_trash_for_duplicates);

// Undo last organization
bool organization_undo(void);

// Selection helpers
void organization_select_category(OrganizationAnalysis *analysis, FileCategory category, bool select);
void organization_select_folder(OrganizationAnalysis *analysis, int folder_index, bool select);
void organization_select_all(OrganizationAnalysis *analysis, bool select);

// Get category name
const char *organization_category_name(FileCategory category);

// Get suggested folder for a file based on its category
const char *organization_get_folder_for_file(const OrganizedFile *file);

// Export analysis as JSON
char *organization_analysis_to_json(const OrganizationAnalysis *analysis);

// Get status message
const char *organization_status_message(OrganizationStatus status);

// Learn from user's organization choices
void organization_learn(const char *file_path, const char *chosen_folder);

#endif // ORGANIZATION_H

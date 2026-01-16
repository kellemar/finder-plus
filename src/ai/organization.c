#include "organization.h"
#include "../api/claude_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

// Undo history
static struct {
    char original_paths[1000][1024];
    char new_paths[1000][1024];
    int count;
    bool valid;
} org_undo_history = {0};

// File extension to category mapping
typedef struct ExtMapping {
    const char *ext;
    FileCategory category;
    const char *subcategory;
} ExtMapping;

static const ExtMapping EXT_MAP[] = {
    // Documents
    {".pdf", CAT_DOCUMENTS, "PDFs"},
    {".doc", CAT_DOCUMENTS, "Word Documents"},
    {".docx", CAT_DOCUMENTS, "Word Documents"},
    {".txt", CAT_DOCUMENTS, "Text Files"},
    {".rtf", CAT_DOCUMENTS, "Text Files"},
    {".md", CAT_DOCUMENTS, "Markdown"},
    {".odt", CAT_DOCUMENTS, "Documents"},
    {".pages", CAT_DOCUMENTS, "Documents"},
    {".xls", CAT_DOCUMENTS, "Spreadsheets"},
    {".xlsx", CAT_DOCUMENTS, "Spreadsheets"},
    {".csv", CAT_DATA, "Data Files"},
    {".ppt", CAT_DOCUMENTS, "Presentations"},
    {".pptx", CAT_DOCUMENTS, "Presentations"},
    {".key", CAT_DOCUMENTS, "Presentations"},

    // Images
    {".jpg", CAT_IMAGES, "Photos"},
    {".jpeg", CAT_IMAGES, "Photos"},
    {".png", CAT_IMAGES, "Images"},
    {".gif", CAT_IMAGES, "GIFs"},
    {".bmp", CAT_IMAGES, "Images"},
    {".webp", CAT_IMAGES, "Images"},
    {".svg", CAT_IMAGES, "Vector Graphics"},
    {".ico", CAT_IMAGES, "Icons"},
    {".heic", CAT_IMAGES, "Photos"},
    {".raw", CAT_IMAGES, "RAW Photos"},
    {".psd", CAT_IMAGES, "Design Files"},
    {".ai", CAT_IMAGES, "Design Files"},
    {".sketch", CAT_IMAGES, "Design Files"},
    {".fig", CAT_IMAGES, "Design Files"},

    // Videos
    {".mp4", CAT_VIDEOS, "Videos"},
    {".mov", CAT_VIDEOS, "Videos"},
    {".avi", CAT_VIDEOS, "Videos"},
    {".mkv", CAT_VIDEOS, "Videos"},
    {".wmv", CAT_VIDEOS, "Videos"},
    {".webm", CAT_VIDEOS, "Videos"},
    {".m4v", CAT_VIDEOS, "Videos"},

    // Audio
    {".mp3", CAT_AUDIO, "Music"},
    {".wav", CAT_AUDIO, "Audio"},
    {".flac", CAT_AUDIO, "Music"},
    {".aac", CAT_AUDIO, "Audio"},
    {".m4a", CAT_AUDIO, "Audio"},
    {".ogg", CAT_AUDIO, "Audio"},
    {".wma", CAT_AUDIO, "Audio"},

    // Archives
    {".zip", CAT_ARCHIVES, "Archives"},
    {".rar", CAT_ARCHIVES, "Archives"},
    {".7z", CAT_ARCHIVES, "Archives"},
    {".tar", CAT_ARCHIVES, "Archives"},
    {".gz", CAT_ARCHIVES, "Archives"},
    {".bz2", CAT_ARCHIVES, "Archives"},
    {".dmg", CAT_APPLICATIONS, "Installers"},
    {".pkg", CAT_APPLICATIONS, "Installers"},
    {".iso", CAT_ARCHIVES, "Disk Images"},

    // Code
    {".c", CAT_CODE, "C"},
    {".h", CAT_CODE, "C"},
    {".cpp", CAT_CODE, "C++"},
    {".hpp", CAT_CODE, "C++"},
    {".py", CAT_CODE, "Python"},
    {".js", CAT_CODE, "JavaScript"},
    {".ts", CAT_CODE, "TypeScript"},
    {".java", CAT_CODE, "Java"},
    {".go", CAT_CODE, "Go"},
    {".rs", CAT_CODE, "Rust"},
    {".rb", CAT_CODE, "Ruby"},
    {".php", CAT_CODE, "PHP"},
    {".swift", CAT_CODE, "Swift"},
    {".kt", CAT_CODE, "Kotlin"},
    {".html", CAT_CODE, "Web"},
    {".css", CAT_CODE, "Web"},
    {".scss", CAT_CODE, "Web"},
    {".vue", CAT_CODE, "Web"},
    {".jsx", CAT_CODE, "React"},
    {".tsx", CAT_CODE, "React"},

    // Data
    {".json", CAT_DATA, "JSON"},
    {".xml", CAT_DATA, "XML"},
    {".yaml", CAT_DATA, "Config"},
    {".yml", CAT_DATA, "Config"},
    {".toml", CAT_DATA, "Config"},
    {".sql", CAT_DATA, "Database"},
    {".db", CAT_DATA, "Database"},
    {".sqlite", CAT_DATA, "Database"},

    // Fonts
    {".ttf", CAT_FONTS, "Fonts"},
    {".otf", CAT_FONTS, "Fonts"},
    {".woff", CAT_FONTS, "Web Fonts"},
    {".woff2", CAT_FONTS, "Web Fonts"},

    // Applications
    {".app", CAT_APPLICATIONS, "Applications"},
    {".exe", CAT_APPLICATIONS, "Executables"},

    {NULL, CAT_OTHER, NULL}
};

// Initialize default configuration
void organization_config_init(OrganizationConfig *config)
{
    if (!config) return;

    memset(config, 0, sizeof(OrganizationConfig));
    config->analyze_content = false;
    config->detect_duplicates = true;
    config->old_file_days = 365;
    config->recursive = false;  // Don't recurse by default for organization
    config->create_subfolders = true;
}

// Create analysis result
OrganizationAnalysis *organization_analysis_create(void)
{
    OrganizationAnalysis *analysis = calloc(1, sizeof(OrganizationAnalysis));
    if (!analysis) return NULL;

    analysis->file_capacity = 256;
    analysis->files = calloc(analysis->file_capacity, sizeof(OrganizedFile));
    if (!analysis->files) {
        free(analysis);
        return NULL;
    }

    return analysis;
}

// Free analysis result
void organization_analysis_free(OrganizationAnalysis *analysis)
{
    if (!analysis) return;
    free(analysis->files);
    free(analysis);
}

// Get category from file extension
static FileCategory get_category_from_extension(const char *ext, const char **subcategory)
{
    if (!ext) {
        if (subcategory) *subcategory = "Other";
        return CAT_OTHER;
    }

    for (int i = 0; EXT_MAP[i].ext; i++) {
        if (strcasecmp(ext, EXT_MAP[i].ext) == 0) {
            if (subcategory) *subcategory = EXT_MAP[i].subcategory;
            return EXT_MAP[i].category;
        }
    }

    if (subcategory) *subcategory = "Other";
    return CAT_OTHER;
}

// Get category name
const char *organization_category_name(FileCategory category)
{
    switch (category) {
        case CAT_DOCUMENTS: return "Documents";
        case CAT_IMAGES: return "Images";
        case CAT_VIDEOS: return "Videos";
        case CAT_AUDIO: return "Audio";
        case CAT_ARCHIVES: return "Archives";
        case CAT_CODE: return "Code";
        case CAT_DATA: return "Data";
        case CAT_APPLICATIONS: return "Applications";
        case CAT_FONTS: return "Fonts";
        default: return "Other";
    }
}

// Check if file is a screenshot
static bool is_screenshot(const char *name)
{
    const char *screenshot_patterns[] = {
        "Screenshot", "screenshot", "Screen Shot", "screen shot",
        "Capture", "capture", "Snip", "snip", NULL
    };

    for (int i = 0; screenshot_patterns[i]; i++) {
        if (strstr(name, screenshot_patterns[i])) return true;
    }
    return false;
}

// Add file to analysis
static bool add_file_to_analysis(OrganizationAnalysis *analysis, const char *path,
                                  const struct stat *st, const OrganizationConfig *config)
{
    if (analysis->file_count >= analysis->file_capacity) {
        int new_capacity = analysis->file_capacity * 2;
        if (new_capacity > ORG_MAX_FILES_PER_CATEGORY * ORG_MAX_CATEGORIES) {
            return false;
        }
        OrganizedFile *new_files = realloc(analysis->files, new_capacity * sizeof(OrganizedFile));
        if (!new_files) return false;
        analysis->files = new_files;
        analysis->file_capacity = new_capacity;
    }

    OrganizedFile *file = &analysis->files[analysis->file_count];
    memset(file, 0, sizeof(OrganizedFile));

    strncpy(file->path, path, sizeof(file->path) - 1);

    // Extract filename
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    strncpy(file->name, name, sizeof(file->name) - 1);

    file->size = st->st_size;

    // Categorize
    const char *ext = strrchr(name, '.');
    const char *subcategory = NULL;
    file->category = get_category_from_extension(ext, &subcategory);

    // Special case: screenshots
    if (file->category == CAT_IMAGES && is_screenshot(name)) {
        subcategory = "Screenshots";
    }

    // Check if old
    time_t now = time(NULL);
    int days_old = (now - st->st_mtime) / (24 * 3600);
    file->is_old = (days_old > config->old_file_days);

    // Build suggested folder
    if (config->create_subfolders && subcategory) {
        snprintf(file->suggested_folder, sizeof(file->suggested_folder),
                 "%s/%s", organization_category_name(file->category), subcategory);
    } else {
        strncpy(file->suggested_folder, organization_category_name(file->category),
                sizeof(file->suggested_folder) - 1);
    }

    file->should_move = true;
    analysis->file_count++;
    analysis->total_size += st->st_size;
    analysis->total_files++;

    if (file->is_old) {
        analysis->old_file_count++;
        analysis->old_file_size += st->st_size;
    }

    return true;
}

// Update category statistics
static void update_category_stats(OrganizationAnalysis *analysis)
{
    // Reset stats
    for (int i = 0; i < ORG_MAX_CATEGORIES; i++) {
        memset(&analysis->categories[i], 0, sizeof(CategoryStats));
    }
    analysis->category_count = 0;

    // Aggregate by category
    for (int i = 0; i < analysis->file_count; i++) {
        OrganizedFile *file = &analysis->files[i];
        FileCategory cat = file->category;

        // Find or create category stats
        CategoryStats *stats = NULL;
        for (int j = 0; j < analysis->category_count; j++) {
            if (analysis->categories[j].category == cat) {
                stats = &analysis->categories[j];
                break;
            }
        }

        if (!stats && analysis->category_count < ORG_MAX_CATEGORIES) {
            stats = &analysis->categories[analysis->category_count++];
            stats->category = cat;
            strncpy(stats->name, organization_category_name(cat), sizeof(stats->name) - 1);
        }

        if (stats) {
            stats->file_count++;
            stats->total_size += file->size;
        }
    }
}

// Generate suggested folders
static void generate_suggested_folders(OrganizationAnalysis *analysis)
{
    analysis->folder_count = 0;

    // Collect unique folder suggestions
    for (int i = 0; i < analysis->file_count && analysis->folder_count < ORG_MAX_FOLDERS; i++) {
        OrganizedFile *file = &analysis->files[i];
        if (!file->should_move) continue;

        // Check if folder already suggested
        bool found = false;
        for (int j = 0; j < analysis->folder_count; j++) {
            if (strcmp(analysis->suggested_folders[j].path, file->suggested_folder) == 0) {
                analysis->suggested_folders[j].file_count++;
                analysis->suggested_folders[j].total_size += file->size;
                found = true;
                break;
            }
        }

        if (!found) {
            SuggestedFolder *folder = &analysis->suggested_folders[analysis->folder_count++];
            strncpy(folder->path, file->suggested_folder, sizeof(folder->path) - 1);
            snprintf(folder->description, sizeof(folder->description),
                     "Organize %s files", file->suggested_folder);
            folder->file_count = 1;
            folder->total_size = file->size;
            folder->selected = true;

            // Check if folder exists
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s",
                     analysis->source_path, file->suggested_folder);
            struct stat st;
            folder->exists = (stat(full_path, &st) == 0);
        }
    }
}

// Analyze directory
OrganizationStatus organization_analyze(const char *path,
                                         const OrganizationConfig *config,
                                         OrganizationProgressCallback progress,
                                         void *user_data,
                                         OrganizationAnalysis *result)
{
    if (!path || !config || !result) {
        return ORG_STATUS_NOT_INITIALIZED;
    }

    clock_t start = clock();

    strncpy(result->source_path, path, sizeof(result->source_path) - 1);

    DIR *dir = opendir(path);
    if (!dir) return ORG_STATUS_FILE_ERROR;

    // First pass: count files
    int total_files = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (entry->d_type == DT_REG) total_files++;
    }
    rewinddir(dir);

    // Second pass: analyze files
    int processed = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(full_path, &st) != 0) continue;

        // Skip directories and symlinks for now
        if (!S_ISREG(st.st_mode)) continue;

        // Check exclusions
        bool excluded = false;
        if (config->exclude_patterns) {
            for (int i = 0; i < config->exclude_count; i++) {
                if (strstr(entry->d_name, config->exclude_patterns[i])) {
                    excluded = true;
                    break;
                }
            }
        }
        if (excluded) continue;

        add_file_to_analysis(result, full_path, &st, config);

        processed++;
        if (progress) {
            progress(processed, total_files, entry->d_name, user_data);
        }
    }

    closedir(dir);

    // Update statistics
    update_category_stats(result);

    // Generate folder suggestions
    generate_suggested_folders(result);

    // Calculate cleanup suggestions
    result->suggested_cleanup_size = result->old_file_size + result->duplicate_size;
    result->suggested_cleanup_count = result->old_file_count + result->duplicate_count;

    clock_t end = clock();
    result->analysis_time_ms = (float)(end - start) / CLOCKS_PER_SEC * 1000.0f;
    result->status = ORG_STATUS_OK;

    return ORG_STATUS_OK;
}

// Generate AI-powered organization suggestions
OrganizationStatus organization_ai_suggest(OrganizationAnalysis *analysis,
                                            const OrganizationConfig *config)
{
    if (!analysis || !config || strlen(config->api_key) == 0) {
        return ORG_STATUS_NOT_INITIALIZED;
    }

    // Build prompt for Claude
    char prompt[8192];
    char *p = prompt;
    size_t remaining = sizeof(prompt);

    int written = snprintf(p, remaining,
        "Analyze this directory and suggest how to organize the files better.\n"
        "Current files:\n");
    p += written;
    remaining -= written;

    // List files (limit to avoid token overflow)
    int listed = 0;
    for (int i = 0; i < analysis->file_count && listed < 50 && remaining > 256; i++) {
        written = snprintf(p, remaining, "- %s (%llu bytes, category: %s)\n",
                           analysis->files[i].name,
                           (unsigned long long)analysis->files[i].size,
                           organization_category_name(analysis->files[i].category));
        p += written;
        remaining -= written;
        listed++;
    }

    snprintf(p, remaining,
        "\nSuggest a folder structure and where each file should go. "
        "Consider grouping by type, date, or project. "
        "Respond with JSON: [{\"file\": \"filename\", \"folder\": \"suggested/path\", \"reason\": \"why\"}]");

    // Send to Claude
    ClaudeClient *client = claude_client_create(config->api_key);
    if (!client) return ORG_STATUS_API_ERROR;

    ClaudeMessageRequest req;
    ClaudeMessageResponse resp;
    claude_request_init(&req);
    claude_response_init(&resp);

    claude_request_set_system_prompt(&req, "You are a file organization assistant. Suggest clean folder structures.");
    claude_request_add_user_message(&req, prompt);

    bool success = claude_send_message(client, &req, &resp);

    if (success && resp.stop_reason != CLAUDE_STOP_ERROR) {
        // Parse suggestions and update files
        // Simple parsing - look for folder suggestions in JSON
        const char *response = resp.content;
        for (int i = 0; i < analysis->file_count; i++) {
            // Look for this file in response
            const char *file_match = strstr(response, analysis->files[i].name);
            if (file_match) {
                // Look for folder suggestion
                const char *folder_key = strstr(file_match, "\"folder\"");
                if (folder_key) {
                    const char *folder_start = strchr(folder_key + 8, '"');
                    if (folder_start) {
                        folder_start++;
                        const char *folder_end = strchr(folder_start, '"');
                        if (folder_end) {
                            size_t len = folder_end - folder_start;
                            if (len < sizeof(analysis->files[i].suggested_folder)) {
                                strncpy(analysis->files[i].suggested_folder, folder_start, len);
                                analysis->files[i].suggested_folder[len] = '\0';
                            }
                        }
                    }
                }
            }
        }

        // Regenerate folder suggestions
        generate_suggested_folders(analysis);
    }

    claude_request_cleanup(&req);
    claude_response_cleanup(&resp);
    claude_client_destroy(client);

    return success ? ORG_STATUS_OK : ORG_STATUS_API_ERROR;
}

// Preview organization plan
char *organization_preview_plan(const OrganizationAnalysis *analysis)
{
    if (!analysis) return NULL;

    size_t size = 4096 + analysis->file_count * 256;
    char *plan = malloc(size);
    if (!plan) return NULL;

    char *p = plan;
    size_t remaining = size;

    int written = snprintf(p, remaining,
        "Organization Plan for: %s\n"
        "========================\n\n"
        "Summary:\n"
        "  Total files: %d\n"
        "  Total size: %.2f MB\n"
        "  Categories: %d\n"
        "  Folders to create: %d\n\n"
        "Folder Structure:\n",
        analysis->source_path,
        analysis->total_files,
        (double)analysis->total_size / (1024 * 1024),
        analysis->category_count,
        analysis->folder_count);
    p += written;
    remaining -= written;

    for (int i = 0; i < analysis->folder_count && remaining > 256; i++) {
        const SuggestedFolder *folder = &analysis->suggested_folders[i];
        written = snprintf(p, remaining,
            "  %s %s (%d files, %.2f MB)%s\n",
            folder->selected ? "[x]" : "[ ]",
            folder->path,
            folder->file_count,
            (double)folder->total_size / (1024 * 1024),
            folder->exists ? " (exists)" : "");
        p += written;
        remaining -= written;
    }

    if (remaining > 256) {
        written = snprintf(p, remaining, "\nFile Moves:\n");
        p += written;
        remaining -= written;

        int shown = 0;
        for (int i = 0; i < analysis->file_count && remaining > 128 && shown < 20; i++) {
            const OrganizedFile *file = &analysis->files[i];
            if (!file->should_move) continue;

            written = snprintf(p, remaining, "  %s -> %s/\n",
                               file->name, file->suggested_folder);
            p += written;
            remaining -= written;
            shown++;
        }

        if (shown < analysis->file_count) {
            snprintf(p, remaining, "  ... and %d more files\n",
                     analysis->file_count - shown);
        }
    }

    return plan;
}

// Create directory recursively
static bool create_directory_recursive(const char *path)
{
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

// Execute organization plan
OrganizationResult organization_execute(OrganizationAnalysis *analysis, bool use_trash_for_duplicates)
{
    OrganizationResult result = {0};

    if (!analysis) {
        result.status = ORG_STATUS_NOT_INITIALIZED;
        return result;
    }

    // Clear undo history
    org_undo_history.count = 0;
    org_undo_history.valid = false;

    // Create folders
    for (int i = 0; i < analysis->folder_count; i++) {
        SuggestedFolder *folder = &analysis->suggested_folders[i];
        if (!folder->selected || folder->exists) continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s",
                 analysis->source_path, folder->path);

        if (create_directory_recursive(full_path)) {
            result.folders_created++;
        }
    }

    // Move files
    for (int i = 0; i < analysis->file_count; i++) {
        OrganizedFile *file = &analysis->files[i];
        if (!file->should_move) continue;

        // Skip duplicates if using trash
        if (file->is_duplicate && use_trash_for_duplicates) {
            // Move to trash
            char cmd[2048];
            snprintf(cmd, sizeof(cmd),
                     "osascript -e 'tell app \"Finder\" to delete POSIX file \"%s\"' 2>/dev/null",
                     file->path);
            if (system(cmd) == 0) {
                result.files_moved++;
                result.bytes_moved += file->size;
            }
            continue;
        }

        // Build destination path
        char dest_dir[1024];
        snprintf(dest_dir, sizeof(dest_dir), "%s/%s",
                 analysis->source_path, file->suggested_folder);

        char dest_path[1024];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, file->name);

        // Skip if same location
        if (strcmp(file->path, dest_path) == 0) continue;

        // Move file
        if (rename(file->path, dest_path) == 0) {
            result.files_moved++;
            result.bytes_moved += file->size;

            // Store in undo history
            if (org_undo_history.count < 1000) {
                strncpy(org_undo_history.original_paths[org_undo_history.count],
                        file->path, 1023);
                strncpy(org_undo_history.new_paths[org_undo_history.count],
                        dest_path, 1023);
                org_undo_history.count++;
            }

            // Learn from this organization
            organization_learn(file->path, file->suggested_folder);
        } else {
            result.errors++;
        }
    }

    org_undo_history.valid = (org_undo_history.count > 0);
    result.status = (result.errors == 0) ? ORG_STATUS_OK : ORG_STATUS_FILE_ERROR;
    return result;
}

// Undo last organization
bool organization_undo(void)
{
    if (!org_undo_history.valid || org_undo_history.count == 0) {
        return false;
    }

    int undone = 0;
    for (int i = 0; i < org_undo_history.count; i++) {
        if (rename(org_undo_history.new_paths[i],
                   org_undo_history.original_paths[i]) == 0) {
            undone++;
        }
    }

    org_undo_history.valid = false;
    org_undo_history.count = 0;
    return undone > 0;
}

// Selection helpers
void organization_select_category(OrganizationAnalysis *analysis, FileCategory category, bool select)
{
    if (!analysis) return;

    for (int i = 0; i < analysis->file_count; i++) {
        if (analysis->files[i].category == category) {
            analysis->files[i].should_move = select;
        }
    }
}

void organization_select_folder(OrganizationAnalysis *analysis, int folder_index, bool select)
{
    if (!analysis || folder_index < 0 || folder_index >= analysis->folder_count) return;
    analysis->suggested_folders[folder_index].selected = select;
}

void organization_select_all(OrganizationAnalysis *analysis, bool select)
{
    if (!analysis) return;

    for (int i = 0; i < analysis->file_count; i++) {
        analysis->files[i].should_move = select;
    }

    for (int i = 0; i < analysis->folder_count; i++) {
        analysis->suggested_folders[i].selected = select;
    }
}

// Get suggested folder for a file
const char *organization_get_folder_for_file(const OrganizedFile *file)
{
    if (!file) return "Other";
    return file->suggested_folder;
}

// Export analysis as JSON
char *organization_analysis_to_json(const OrganizationAnalysis *analysis)
{
    if (!analysis) return NULL;

    size_t size = 8192 + analysis->file_count * 512;
    char *json = malloc(size);
    if (!json) return NULL;

    char *p = json;
    size_t remaining = size;

    int written = snprintf(p, remaining, "{\n");
    p += written;
    remaining -= written;

    written = snprintf(p, remaining,
        "  \"source_path\": \"%s\",\n"
        "  \"total_files\": %d,\n"
        "  \"total_size\": %llu,\n"
        "  \"analysis_time_ms\": %.2f,\n",
        analysis->source_path,
        analysis->total_files,
        (unsigned long long)analysis->total_size,
        analysis->analysis_time_ms);
    p += written;
    remaining -= written;

    // Categories
    written = snprintf(p, remaining, "  \"categories\": [\n");
    p += written;
    remaining -= written;

    for (int i = 0; i < analysis->category_count && remaining > 256; i++) {
        const CategoryStats *cat = &analysis->categories[i];
        written = snprintf(p, remaining,
            "    {\"name\": \"%s\", \"file_count\": %d, \"total_size\": %llu}%s\n",
            cat->name, cat->file_count, (unsigned long long)cat->total_size,
            i < analysis->category_count - 1 ? "," : "");
        p += written;
        remaining -= written;
    }

    written = snprintf(p, remaining, "  ],\n");
    p += written;
    remaining -= written;

    // Suggested folders
    written = snprintf(p, remaining, "  \"suggested_folders\": [\n");
    p += written;
    remaining -= written;

    for (int i = 0; i < analysis->folder_count && remaining > 256; i++) {
        const SuggestedFolder *folder = &analysis->suggested_folders[i];
        written = snprintf(p, remaining,
            "    {\"path\": \"%s\", \"file_count\": %d, \"exists\": %s}%s\n",
            folder->path, folder->file_count, folder->exists ? "true" : "false",
            i < analysis->folder_count - 1 ? "," : "");
        p += written;
        remaining -= written;
    }

    written = snprintf(p, remaining, "  ]\n}\n");
    p += written;

    return json;
}

// Get status message
const char *organization_status_message(OrganizationStatus status)
{
    switch (status) {
        case ORG_STATUS_OK: return "OK";
        case ORG_STATUS_NOT_INITIALIZED: return "Not initialized";
        case ORG_STATUS_FILE_ERROR: return "File error";
        case ORG_STATUS_API_ERROR: return "API error";
        case ORG_STATUS_MEMORY_ERROR: return "Memory error";
        case ORG_STATUS_CANCELLED: return "Cancelled";
        default: return "Unknown";
    }
}

// Learn from user's organization choices
void organization_learn(const char *file_path, const char *chosen_folder)
{
    // Placeholder for learning functionality
    // In a full implementation, this would store patterns in SQLite
    (void)file_path;
    (void)chosen_folder;
}

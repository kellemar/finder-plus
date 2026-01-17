#include "duplicates.h"
#include "embeddings.h"
#include "ai_common.h"
#include "../platform/trash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <CommonCrypto/CommonDigest.h>
#include <time.h>

// Internal file list for scanning
typedef struct FileList {
    DuplicateFileInfo *files;
    int count;
    int capacity;
} FileList;

// Initialize configuration with defaults
void duplicate_config_init(DuplicateConfig *config)
{
    if (!config) return;

    config->detect_exact = true;
    config->detect_similar_images = true;
    config->detect_similar_text = true;
    config->similarity_threshold = 0.90f;
    config->min_file_size = 1;
    config->max_file_size = 1024 * 1024 * 1024; // 1GB
    config->recursive = true;
    config->exclude_patterns = NULL;
    config->exclude_count = 0;
    config->cancelled = false;
}

// Create analysis result
DuplicateAnalysis *duplicate_analysis_create(void)
{
    DuplicateAnalysis *analysis = calloc(1, sizeof(DuplicateAnalysis));
    if (!analysis) return NULL;

    analysis->group_capacity = 64;
    analysis->groups = calloc(analysis->group_capacity, sizeof(DuplicateGroup));
    if (!analysis->groups) {
        free(analysis);
        return NULL;
    }

    analysis->status = DUP_STATUS_OK;
    return analysis;
}

// Free duplicate group
static void duplicate_group_free(DuplicateGroup *group)
{
    if (!group) return;
    free(group->files);
    group->files = NULL;
    group->file_count = 0;
    group->capacity = 0;
}

// Free analysis result
void duplicate_analysis_free(DuplicateAnalysis *analysis)
{
    if (!analysis) return;

    for (int i = 0; i < analysis->group_count; i++) {
        duplicate_group_free(&analysis->groups[i]);
    }
    free(analysis->groups);
    free(analysis);
}

// Hash file using MD5
bool hash_file_md5(const char *path, uint8_t *hash_out)
{
    if (!path || !hash_out) return false;

    FILE *file = fopen(path, "rb");
    if (!file) return false;

    CC_MD5_CTX ctx;
    CC_MD5_Init(&ctx);

    unsigned char buffer[8192];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        CC_MD5_Update(&ctx, buffer, (CC_LONG)bytes);
    }

    fclose(file);
    CC_MD5_Final(hash_out, &ctx);
    return true;
}

// Hash file using SHA256
bool hash_file_sha256(const char *path, uint8_t *hash_out)
{
    if (!path || !hash_out) return false;

    FILE *file = fopen(path, "rb");
    if (!file) return false;

    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);

    unsigned char buffer[8192];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        CC_SHA256_Update(&ctx, buffer, (CC_LONG)bytes);
    }

    fclose(file);
    CC_SHA256_Final(hash_out, &ctx);
    return true;
}

// Simple perceptual hash for images using average hash algorithm
// This is a simplified version - in production, use a proper image library
bool hash_image_perceptual(const char *path, uint8_t *hash_out)
{
    if (!path || !hash_out) return false;

    // Check if it's an image by extension
    const char *ext = strrchr(path, '.');
    if (!ext) return false;

    // For now, use file content hash as a fallback
    // In a real implementation, we would:
    // 1. Load the image
    // 2. Resize to 8x8
    // 3. Convert to grayscale
    // 4. Calculate average pixel value
    // 5. Generate hash based on above/below average

    // Simplified: use first 8 bytes of MD5 hash
    uint8_t md5[HASH_SIZE_MD5];
    if (!hash_file_md5(path, md5)) return false;

    memcpy(hash_out, md5, HASH_SIZE_PERCEPTUAL);
    return true;
}

// Calculate Hamming distance between two hashes
int hash_hamming_distance(const uint8_t *hash1, const uint8_t *hash2, size_t len)
{
    if (!hash1 || !hash2) return -1;

    int distance = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t xor_val = hash1[i] ^ hash2[i];
        while (xor_val) {
            distance += xor_val & 1;
            xor_val >>= 1;
        }
    }
    return distance;
}

// Compare two files byte-by-byte
bool files_are_identical(const char *path1, const char *path2)
{
    if (!path1 || !path2) return false;

    struct stat st1, st2;
    if (stat(path1, &st1) != 0 || stat(path2, &st2) != 0) return false;

    // Different sizes = not identical
    if (st1.st_size != st2.st_size) return false;

    FILE *f1 = fopen(path1, "rb");
    FILE *f2 = fopen(path2, "rb");
    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return false;
    }

    bool identical = true;
    unsigned char buf1[8192], buf2[8192];
    size_t bytes1, bytes2;

    while ((bytes1 = fread(buf1, 1, sizeof(buf1), f1)) > 0) {
        bytes2 = fread(buf2, 1, sizeof(buf2), f2);
        if (bytes1 != bytes2 || memcmp(buf1, buf2, bytes1) != 0) {
            identical = false;
            break;
        }
    }

    fclose(f1);
    fclose(f2);
    return identical;
}

// Hash to hex string
void hash_to_hex(const uint8_t *hash, size_t len, char *hex_out)
{
    if (!hash || !hex_out) return;

    for (size_t i = 0; i < len; i++) {
        sprintf(hex_out + i * 2, "%02x", hash[i]);
    }
    hex_out[len * 2] = '\0';
}

// Check if file should be excluded
static bool should_exclude(const char *path, const DuplicateConfig *config)
{
    if (!config->exclude_patterns || config->exclude_count == 0) return false;

    for (int i = 0; i < config->exclude_count; i++) {
        if (strstr(path, config->exclude_patterns[i])) {
            return true;
        }
    }
    return false;
}

// Check if file is an image
static bool is_image_file(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return false;

    const char *image_exts[] = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".tiff", ".tif", NULL};
    for (int i = 0; image_exts[i]; i++) {
        if (strcasecmp(ext, image_exts[i]) == 0) return true;
    }
    return false;
}

// Check if file is a text file
static bool is_text_file(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return false;

    const char *text_exts[] = {".txt", ".md", ".c", ".h", ".py", ".js", ".json", ".xml", ".html", ".css", ".yml", ".yaml", NULL};
    for (int i = 0; text_exts[i]; i++) {
        if (strcasecmp(ext, text_exts[i]) == 0) return true;
    }
    return false;
}

// Add file to list
static bool file_list_add(FileList *list, const char *path, const struct stat *st)
{
    if (list->count >= list->capacity) {
        int new_capacity = list->capacity * 2;
        DuplicateFileInfo *new_files = realloc(list->files, new_capacity * sizeof(DuplicateFileInfo));
        if (!new_files) return false;
        list->files = new_files;
        list->capacity = new_capacity;
    }

    DuplicateFileInfo *info = &list->files[list->count];
    memset(info, 0, sizeof(DuplicateFileInfo));
    strncpy(info->path, path, sizeof(info->path) - 1);
    info->size = st->st_size;
    info->modified = st->st_mtime;
    info->accessed = st->st_atime;

    list->count++;
    return true;
}

// Recursively scan directory and collect files
static DuplicateStatus scan_directory_recursive(const char *path,
                                                  const DuplicateConfig *config,
                                                  FileList *list,
                                                  DuplicateProgressCallback progress,
                                                  void *user_data)
{
    if (config->cancelled) return DUP_STATUS_CANCELLED;

    DIR *dir = opendir(path);
    if (!dir) return DUP_STATUS_FILE_ERROR;

    struct dirent *entry;
    char full_path[1024];

    while ((entry = readdir(dir)) != NULL) {
        if (config->cancelled) {
            closedir(dir);
            return DUP_STATUS_CANCELLED;
        }

        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        // Build full path
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        // Check exclusions
        if (should_exclude(full_path, config)) continue;

        struct stat st;
        if (lstat(full_path, &st) != 0) continue;

        // Skip symlinks
        if (S_ISLNK(st.st_mode)) continue;

        if (S_ISDIR(st.st_mode)) {
            // Recurse into directory
            if (config->recursive) {
                DuplicateStatus status = scan_directory_recursive(full_path, config, list, progress, user_data);
                if (status != DUP_STATUS_OK) {
                    closedir(dir);
                    return status;
                }
            }
        } else if (S_ISREG(st.st_mode)) {
            // Check size constraints
            if (st.st_size < config->min_file_size || st.st_size > config->max_file_size) continue;

            // Check file count limit
            if (list->count >= DUPLICATES_MAX_FILES) {
                closedir(dir);
                return DUP_STATUS_TOO_MANY_FILES;
            }

            // Add file to list
            if (!file_list_add(list, full_path, &st)) {
                closedir(dir);
                return DUP_STATUS_MEMORY_ERROR;
            }

            // Report progress
            if (progress) {
                progress(list->count, 0, full_path, user_data);
            }
        }
    }

    closedir(dir);
    return DUP_STATUS_OK;
}

// Add file to duplicate group
static bool add_to_group(DuplicateGroup *group, const DuplicateFileInfo *file)
{
    if (group->file_count >= group->capacity) {
        int new_capacity = group->capacity == 0 ? 4 : group->capacity * 2;
        DuplicateFileInfo *new_files = realloc(group->files, new_capacity * sizeof(DuplicateFileInfo));
        if (!new_files) return false;
        group->files = new_files;
        group->capacity = new_capacity;
    }

    group->files[group->file_count] = *file;
    group->file_count++;
    group->total_size += file->size;
    return true;
}

// Add group to analysis
static DuplicateGroup *add_group(DuplicateAnalysis *analysis, DuplicateType type)
{
    if (analysis->group_count >= analysis->group_capacity) {
        int new_capacity = analysis->group_capacity * 2;
        DuplicateGroup *new_groups = realloc(analysis->groups, new_capacity * sizeof(DuplicateGroup));
        if (!new_groups) return NULL;
        analysis->groups = new_groups;
        analysis->group_capacity = new_capacity;
    }

    DuplicateGroup *group = &analysis->groups[analysis->group_count];
    memset(group, 0, sizeof(DuplicateGroup));
    group->type = type;
    analysis->group_count++;
    return group;
}

// Find exact duplicates by hash
static void find_exact_duplicates(FileList *list, DuplicateAnalysis *analysis,
                                   DuplicateProgressCallback progress, void *user_data)
{
    // First pass: compute hashes
    for (int i = 0; i < list->count; i++) {
        DuplicateFileInfo *file = &list->files[i];
        hash_file_md5(file->path, file->hash_md5);

        if (progress) {
            progress(i + 1, list->count, file->path, user_data);
        }
    }

    // Group by hash
    bool *processed = calloc(list->count, sizeof(bool));
    if (!processed) return;

    for (int i = 0; i < list->count; i++) {
        if (processed[i]) continue;

        DuplicateFileInfo *file1 = &list->files[i];
        DuplicateGroup *group = NULL;

        for (int j = i + 1; j < list->count; j++) {
            if (processed[j]) continue;

            DuplicateFileInfo *file2 = &list->files[j];

            // Same size is a prerequisite
            if (file1->size != file2->size) continue;

            // Compare hashes
            if (memcmp(file1->hash_md5, file2->hash_md5, HASH_SIZE_MD5) == 0) {
                // Verify with byte comparison for safety
                if (files_are_identical(file1->path, file2->path)) {
                    if (!group) {
                        group = add_group(analysis, DUP_TYPE_EXACT);
                        if (!group) {
                            free(processed);
                            return;
                        }
                        add_to_group(group, file1);
                    }
                    add_to_group(group, file2);
                    processed[j] = true;
                }
            }
        }

        if (group) {
            processed[i] = true;
            analysis->total_duplicates_found += group->file_count;
            // Calculate reclaimable size (all but one copy)
            group->reclaimable_size = group->total_size - file1->size;
            analysis->total_reclaimable_size += group->reclaimable_size;
        }
    }

    free(processed);
}

// Find similar images using perceptual hash
static void find_similar_images(FileList *list, DuplicateAnalysis *analysis,
                                 const DuplicateConfig *config,
                                 DuplicateProgressCallback progress, void *user_data)
{
    // Filter to only images
    int *image_indices = malloc((size_t)list->count * sizeof(int));
    if (!image_indices) return;
    int image_count = 0;

    for (int i = 0; i < list->count; i++) {
        if (is_image_file(list->files[i].path)) {
            image_indices[image_count++] = i;
        }
    }

    if (image_count < 2) {
        free(image_indices);
        return;
    }

    // Compute perceptual hashes
    for (int i = 0; i < image_count; i++) {
        DuplicateFileInfo *file = &list->files[image_indices[i]];
        hash_image_perceptual(file->path, file->hash_perceptual);

        if (progress) {
            progress(i + 1, image_count, file->path, user_data);
        }
    }

    // Group similar images (Hamming distance threshold)
    int max_distance = (int)((1.0f - config->similarity_threshold) * 64); // 64 bits total
    bool *processed = calloc((size_t)image_count, sizeof(bool));
    if (!processed) {
        free(image_indices);
        return;
    }

    for (int i = 0; i < image_count; i++) {
        if (processed[i]) continue;

        DuplicateFileInfo *file1 = &list->files[image_indices[i]];
        DuplicateGroup *group = NULL;

        for (int j = i + 1; j < image_count; j++) {
            if (processed[j]) continue;

            DuplicateFileInfo *file2 = &list->files[image_indices[j]];
            int distance = hash_hamming_distance(file1->hash_perceptual, file2->hash_perceptual, HASH_SIZE_PERCEPTUAL);

            if (distance >= 0 && distance <= max_distance) {
                if (!group) {
                    group = add_group(analysis, DUP_TYPE_SIMILAR_IMAGE);
                    if (!group) break;
                    file1->similarity_score = 1.0f;
                    add_to_group(group, file1);
                }
                file2->similarity_score = 1.0f - (float)distance / 64.0f;
                add_to_group(group, file2);
                processed[j] = true;
            }
        }

        if (group) {
            processed[i] = true;
            analysis->total_duplicates_found += group->file_count;
            // Reclaimable: keep largest
            uint64_t max_size = 0;
            for (int k = 0; k < group->file_count; k++) {
                if (group->files[k].size > max_size) max_size = group->files[k].size;
            }
            group->reclaimable_size = group->total_size - max_size;
            analysis->total_reclaimable_size += group->reclaimable_size;
        }
    }

    free(processed);
    free(image_indices);
}

// Find similar text files using embeddings
static void find_similar_text(FileList *list, DuplicateAnalysis *analysis,
                               const DuplicateConfig *config,
                               DuplicateProgressCallback progress, void *user_data)
{
    // Filter to only text files
    int *text_indices = malloc((size_t)list->count * sizeof(int));
    if (!text_indices) return;
    int text_count = 0;

    for (int i = 0; i < list->count; i++) {
        if (is_text_file(list->files[i].path)) {
            text_indices[text_count++] = i;
        }
    }

    if (text_count < 2) {
        free(text_indices);
        return;
    }

    // Create embedding engine
    EmbeddingEngine *engine = embedding_engine_create();
    if (!engine) {
        free(text_indices);
        return;
    }

    // Allocate embeddings array
    float *embeddings = malloc(text_count * EMBEDDING_DIMENSION * sizeof(float));
    if (!embeddings) {
        embedding_engine_destroy(engine);
        free(text_indices);
        return;
    }

    // Generate embeddings for each text file
    for (int i = 0; i < text_count; i++) {
        DuplicateFileInfo *file = &list->files[text_indices[i]];

        // Read file content (up to 8KB)
        FILE *f = fopen(file->path, "r");
        if (f) {
            char content[8192] = {0};
            size_t len = fread(content, 1, sizeof(content) - 1, f);
            content[len] = '\0';
            fclose(f);

            EmbeddingResult result = embedding_generate(engine, content);
            if (result.status == EMBEDDING_STATUS_OK) {
                memcpy(&embeddings[i * EMBEDDING_DIMENSION], result.embedding, EMBEDDING_DIMENSION * sizeof(float));
            } else {
                memset(&embeddings[i * EMBEDDING_DIMENSION], 0, EMBEDDING_DIMENSION * sizeof(float));
            }
        }

        if (progress) {
            progress(i + 1, text_count, file->path, user_data);
        }
    }

    // Group similar text files
    bool *processed = calloc((size_t)text_count, sizeof(bool));
    if (!processed) {
        free(embeddings);
        embedding_engine_destroy(engine);
        free(text_indices);
        return;
    }

    for (int i = 0; i < text_count; i++) {
        if (processed[i]) continue;

        DuplicateFileInfo *file1 = &list->files[text_indices[i]];
        float *emb1 = &embeddings[i * EMBEDDING_DIMENSION];
        DuplicateGroup *group = NULL;

        for (int j = i + 1; j < text_count; j++) {
            if (processed[j]) continue;

            DuplicateFileInfo *file2 = &list->files[text_indices[j]];
            float *emb2 = &embeddings[j * EMBEDDING_DIMENSION];

            float similarity = cosine_similarity(emb1, emb2, EMBEDDING_DIMENSION);

            if (similarity >= config->similarity_threshold) {
                if (!group) {
                    group = add_group(analysis, DUP_TYPE_SIMILAR_TEXT);
                    if (!group) break;
                    file1->similarity_score = 1.0f;
                    add_to_group(group, file1);
                }
                file2->similarity_score = similarity;
                add_to_group(group, file2);
                processed[j] = true;
            }
        }

        if (group) {
            processed[i] = true;
            analysis->total_duplicates_found += group->file_count;
            // Reclaimable: keep newest
            time_t newest = 0;
            uint64_t newest_size = 0;
            for (int k = 0; k < group->file_count; k++) {
                if (group->files[k].modified > newest) {
                    newest = group->files[k].modified;
                    newest_size = group->files[k].size;
                }
            }
            group->reclaimable_size = group->total_size - newest_size;
            analysis->total_reclaimable_size += group->reclaimable_size;
        }
    }

    free(processed);
    free(embeddings);
    embedding_engine_destroy(engine);
    free(text_indices);
}

// Scan directory for duplicates
DuplicateStatus duplicate_scan_directory(const char *path,
                                          const DuplicateConfig *config,
                                          DuplicateProgressCallback progress,
                                          void *user_data,
                                          DuplicateAnalysis *result)
{
    return duplicate_scan_directories(&path, 1, config, progress, user_data, result);
}

// Scan multiple directories
DuplicateStatus duplicate_scan_directories(const char **paths,
                                            int path_count,
                                            const DuplicateConfig *config,
                                            DuplicateProgressCallback progress,
                                            void *user_data,
                                            DuplicateAnalysis *result)
{
    if (!paths || path_count <= 0 || !config || !result) {
        return DUP_STATUS_NOT_INITIALIZED;
    }

    clock_t start = clock();

    // Create file list
    FileList list = {0};
    list.capacity = 1024;
    list.files = malloc(list.capacity * sizeof(DuplicateFileInfo));
    if (!list.files) return DUP_STATUS_MEMORY_ERROR;

    // Scan all directories
    for (int i = 0; i < path_count; i++) {
        DuplicateStatus status = scan_directory_recursive(paths[i], config, &list, progress, user_data);
        if (status != DUP_STATUS_OK) {
            free(list.files);
            return status;
        }
    }

    result->total_files_scanned = list.count;

    // Find duplicates
    if (config->detect_exact) {
        find_exact_duplicates(&list, result, progress, user_data);
    }

    if (config->detect_similar_images) {
        find_similar_images(&list, result, config, progress, user_data);
    }

    if (config->detect_similar_text) {
        find_similar_text(&list, result, config, progress, user_data);
    }

    // Suggest which files to keep
    for (int i = 0; i < result->group_count; i++) {
        duplicate_suggest_keep(&result->groups[i], KEEP_NEWEST);
    }

    free(list.files);

    clock_t end = clock();
    result->scan_time_ms = (float)(end - start) / CLOCKS_PER_SEC * 1000.0f;
    result->status = DUP_STATUS_OK;

    return DUP_STATUS_OK;
}

// Find duplicates of a specific file
DuplicateStatus duplicate_find_copies(const char *file_path,
                                       const char *search_dir,
                                       const DuplicateConfig *config,
                                       DuplicateAnalysis *result)
{
    if (!file_path || !search_dir || !config || !result) {
        return DUP_STATUS_NOT_INITIALIZED;
    }

    // Get source file info and hash
    struct stat st;
    if (stat(file_path, &st) != 0) return DUP_STATUS_FILE_ERROR;

    uint8_t source_hash[HASH_SIZE_MD5];
    if (!hash_file_md5(file_path, source_hash)) return DUP_STATUS_FILE_ERROR;

    // Scan directory for matching files
    DuplicateConfig scan_config = *config;
    scan_config.detect_similar_images = false;
    scan_config.detect_similar_text = false;

    FileList list = {0};
    list.capacity = 1024;
    list.files = malloc(list.capacity * sizeof(DuplicateFileInfo));
    if (!list.files) return DUP_STATUS_MEMORY_ERROR;

    DuplicateStatus status = scan_directory_recursive(search_dir, &scan_config, &list, NULL, NULL);
    if (status != DUP_STATUS_OK) {
        free(list.files);
        return status;
    }

    // Find matching files
    DuplicateGroup *group = add_group(result, DUP_TYPE_EXACT);
    if (!group) {
        free(list.files);
        return DUP_STATUS_MEMORY_ERROR;
    }

    // Add source file
    DuplicateFileInfo source_info = {0};
    strncpy(source_info.path, file_path, sizeof(source_info.path) - 1);
    source_info.size = st.st_size;
    source_info.modified = st.st_mtime;
    memcpy(source_info.hash_md5, source_hash, HASH_SIZE_MD5);
    add_to_group(group, &source_info);

    // Find duplicates
    for (int i = 0; i < list.count; i++) {
        DuplicateFileInfo *file = &list.files[i];

        // Skip if same path
        if (strcmp(file->path, file_path) == 0) continue;

        // Check size first
        if ((uint64_t)st.st_size != file->size) continue;

        // Compute hash and compare
        if (hash_file_md5(file->path, file->hash_md5)) {
            if (memcmp(source_hash, file->hash_md5, HASH_SIZE_MD5) == 0) {
                if (files_are_identical(file_path, file->path)) {
                    add_to_group(group, file);
                    result->total_duplicates_found++;
                }
            }
        }
    }

    free(list.files);

    // Remove group if no duplicates found
    if (group->file_count <= 1) {
        duplicate_group_free(group);
        result->group_count--;
    } else {
        group->total_size = group->file_count * st.st_size;
        group->reclaimable_size = group->total_size - st.st_size;
        result->total_reclaimable_size = group->reclaimable_size;
        duplicate_suggest_keep(group, KEEP_NEWEST);
    }

    result->status = DUP_STATUS_OK;
    return DUP_STATUS_OK;
}

// Suggest which file to keep in a group
void duplicate_suggest_keep(DuplicateGroup *group, KeepReason preference)
{
    if (!group || group->file_count == 0) return;

    int best_index = 0;
    DuplicateFileInfo *best = &group->files[0];

    for (int i = 1; i < group->file_count; i++) {
        DuplicateFileInfo *file = &group->files[i];
        bool is_better = false;

        switch (preference) {
            case KEEP_NEWEST:
                is_better = file->modified > best->modified;
                break;
            case KEEP_OLDEST:
                is_better = file->modified < best->modified;
                break;
            case KEEP_LARGEST:
                is_better = file->size > best->size;
                break;
            case KEEP_SHORTEST_PATH:
                is_better = strlen(file->path) < strlen(best->path);
                break;
            case KEEP_MOST_ACCESSED:
                is_better = file->accessed > best->accessed;
                break;
            default:
                break;
        }

        if (is_better) {
            best_index = i;
            best = file;
        }
    }

    // Mark all as not keep, then set the best one
    for (int i = 0; i < group->file_count; i++) {
        group->files[i].is_suggested_keep = (i == best_index);
        group->files[i].keep_reason = preference;
    }

    group->suggested_keep_index = best_index;
    group->keep_reason = preference;
}

// Delete all duplicates except suggested keep (moves to trash)
int duplicate_cleanup_group(DuplicateGroup *group, bool use_trash)
{
    if (!group || group->file_count <= 1) return 0;

    int deleted = 0;
    for (int i = 0; i < group->file_count; i++) {
        if (group->files[i].is_suggested_keep) continue;

        bool success;
        if (use_trash) {
            // Move to trash using native macOS API (safe from shell injection)
            success = platform_move_to_trash(group->files[i].path);
        } else {
            success = (remove(group->files[i].path) == 0);
        }

        if (success) deleted++;
    }

    return deleted;
}

// Export analysis to JSON
char *duplicate_analysis_to_json(const DuplicateAnalysis *analysis)
{
    if (!analysis) return NULL;

    // Estimate size needed
    size_t size = 4096 + analysis->group_count * 2048;
    char *json = malloc(size);
    if (!json) return NULL;

    char *p = json;
    p += sprintf(p, "{\n");
    p += sprintf(p, "  \"status\": \"%s\",\n", duplicate_status_message(analysis->status));
    p += sprintf(p, "  \"total_files_scanned\": %d,\n", analysis->total_files_scanned);
    p += sprintf(p, "  \"total_duplicates_found\": %d,\n", analysis->total_duplicates_found);
    p += sprintf(p, "  \"total_reclaimable_bytes\": %llu,\n", analysis->total_reclaimable_size);
    p += sprintf(p, "  \"scan_time_ms\": %.2f,\n", analysis->scan_time_ms);
    p += sprintf(p, "  \"groups\": [\n");

    for (int i = 0; i < analysis->group_count; i++) {
        DuplicateGroup *group = &analysis->groups[i];
        const char *type_str = group->type == DUP_TYPE_EXACT ? "exact" :
                               group->type == DUP_TYPE_SIMILAR_IMAGE ? "similar_image" : "similar_text";

        p += sprintf(p, "    {\n");
        p += sprintf(p, "      \"type\": \"%s\",\n", type_str);
        p += sprintf(p, "      \"file_count\": %d,\n", group->file_count);
        p += sprintf(p, "      \"total_size\": %llu,\n", group->total_size);
        p += sprintf(p, "      \"reclaimable_size\": %llu,\n", group->reclaimable_size);
        p += sprintf(p, "      \"files\": [\n");

        for (int j = 0; j < group->file_count; j++) {
            DuplicateFileInfo *file = &group->files[j];
            p += sprintf(p, "        {\n");
            p += sprintf(p, "          \"path\": \"%s\",\n", file->path);
            p += sprintf(p, "          \"size\": %llu,\n", file->size);
            p += sprintf(p, "          \"similarity\": %.3f,\n", file->similarity_score);
            p += sprintf(p, "          \"suggested_keep\": %s\n", file->is_suggested_keep ? "true" : "false");
            p += sprintf(p, "        }%s\n", j < group->file_count - 1 ? "," : "");
        }

        p += sprintf(p, "      ]\n");
        p += sprintf(p, "    }%s\n", i < analysis->group_count - 1 ? "," : "");
    }

    p += sprintf(p, "  ]\n");
    p += sprintf(p, "}\n");

    return json;
}

// Get status message
const char *duplicate_status_message(DuplicateStatus status)
{
    switch (status) {
        case DUP_STATUS_OK: return "OK";
        case DUP_STATUS_NOT_INITIALIZED: return "Not initialized";
        case DUP_STATUS_FILE_ERROR: return "File error";
        case DUP_STATUS_MEMORY_ERROR: return "Memory error";
        case DUP_STATUS_CANCELLED: return "Cancelled";
        case DUP_STATUS_TOO_MANY_FILES: return "Too many files";
        default: return "Unknown";
    }
}

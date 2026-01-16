#include "visual_search.h"
#include "ai_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

// SQL for image-specific queries
static const char *SQL_CREATE_IMAGES_TABLE =
    "CREATE TABLE IF NOT EXISTS image_index ("
    "  path TEXT PRIMARY KEY,"
    "  name TEXT NOT NULL,"
    "  embedding BLOB,"
    "  width INTEGER,"
    "  height INTEGER,"
    "  size INTEGER,"
    "  modified_time INTEGER"
    ");";

static const char *SQL_INSERT_IMAGE =
    "INSERT OR REPLACE INTO image_index "
    "(path, name, embedding, width, height, size, modified_time) "
    "VALUES (?, ?, ?, ?, ?, ?, ?);";

static const char *SQL_GET_ALL_IMAGES =
    "SELECT path, name, embedding, width, height, size FROM image_index;";

static const char *SQL_GET_IMAGES_IN_DIR =
    "SELECT path, name, embedding, width, height, size FROM image_index "
    "WHERE path LIKE ? || '%';";

static const char *SQL_COUNT_IMAGES =
    "SELECT COUNT(*) FROM image_index;";

static const char *SQL_IS_IMAGE_INDEXED =
    "SELECT 1 FROM image_index WHERE path = ? AND modified_time >= ?;";

// Visual search internal structure
struct VisualSearch {
    CLIPEngine *clip_engine;
    VectorDB *vectordb;
    sqlite3 *db;        // Direct DB handle for image-specific queries
    bool initialized;
};

// Forward declaration
static bool init_image_table(VisualSearch *vs);

VisualSearch* visual_search_create(void)
{
    VisualSearch *vs = calloc(1, sizeof(VisualSearch));
    if (vs == NULL) {
        return NULL;
    }

    vs->initialized = true;
    return vs;
}

void visual_search_destroy(VisualSearch *vs)
{
    if (vs == NULL) {
        return;
    }

    // We don't own the engine or db - just clear refs
    vs->clip_engine = NULL;
    vs->vectordb = NULL;
    vs->db = NULL;

    free(vs);
}

void visual_search_set_clip_engine(VisualSearch *vs, CLIPEngine *engine)
{
    if (vs == NULL) {
        return;
    }
    vs->clip_engine = engine;
}

void visual_search_set_vectordb(VisualSearch *vs, VectorDB *db)
{
    if (vs == NULL) {
        return;
    }
    vs->vectordb = db;

    // Get direct DB handle and initialize image table
    if (db != NULL) {
        vs->db = vectordb_get_db_handle(db);
        init_image_table(vs);
    }
}

static bool init_image_table(VisualSearch *vs)
{
    if (vs == NULL || vs->db == NULL) {
        return false;
    }

    char *err_msg = NULL;
    int rc = sqlite3_exec(vs->db, SQL_CREATE_IMAGES_TABLE, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }

    return true;
}

VisualSearchOptions visual_search_default_options(void)
{
    VisualSearchOptions options = {
        .max_results = 20,
        .min_score = 0.0f,
        .directory = NULL
    };
    return options;
}

static VisualSearchResults create_error_result(const char *error)
{
    VisualSearchResults results = {0};
    results.success = false;
    strncpy(results.error_message, error, sizeof(results.error_message) - 1);
    return results;
}

static float get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0f + ts.tv_nsec / 1000000.0f;
}

// Comparison function for sorting by score (descending)
static int compare_results_by_score(const void *a, const void *b)
{
    const VisualSearchResult *ra = (const VisualSearchResult *)a;
    const VisualSearchResult *rb = (const VisualSearchResult *)b;

    if (rb->score > ra->score) return 1;
    if (rb->score < ra->score) return -1;
    return 0;
}

VisualSearchResults visual_search_query(VisualSearch *vs,
                                         const char *query,
                                         const VisualSearchOptions *options)
{
    if (vs == NULL || !vs->initialized) {
        return create_error_result("Visual search not initialized");
    }

    if (vs->clip_engine == NULL) {
        return create_error_result("No CLIP engine set");
    }

    if (!clip_engine_is_loaded(vs->clip_engine)) {
        return create_error_result("CLIP model not loaded");
    }

    if (vs->db == NULL) {
        return create_error_result("No database set");
    }

    if (query == NULL || query[0] == '\0') {
        return create_error_result("Query is empty");
    }

    // Use default options if not provided
    VisualSearchOptions opts;
    if (options != NULL) {
        opts = *options;
    } else {
        opts = visual_search_default_options();
    }

    float start_time = get_time_ms();

    // Generate text embedding
    CLIPTextResult text_result = clip_embed_text(vs->clip_engine, query);
    if (text_result.status != CLIP_STATUS_OK) {
        return create_error_result(clip_status_message(text_result.status));
    }

    // Query images from database
    sqlite3_stmt *stmt;
    const char *sql = (opts.directory != NULL) ? SQL_GET_IMAGES_IN_DIR : SQL_GET_ALL_IMAGES;

    int rc = sqlite3_prepare_v2(vs->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return create_error_result("Database query error");
    }

    if (opts.directory != NULL) {
        sqlite3_bind_text(stmt, 1, opts.directory, -1, SQLITE_STATIC);
    }

    // Allocate results array
    int capacity = opts.max_results > 0 ? opts.max_results * 2 : 100;
    VisualSearchResults results = {0};
    results.results = calloc((size_t)capacity, sizeof(VisualSearchResult));
    if (results.results == NULL) {
        sqlite3_finalize(stmt);
        return create_error_result("Memory allocation error");
    }
    results.capacity = capacity;

    // Iterate through images and compute similarities
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *path = (const char *)sqlite3_column_text(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const void *embedding_blob = sqlite3_column_blob(stmt, 2);
        int blob_size = sqlite3_column_bytes(stmt, 2);
        int width = sqlite3_column_int(stmt, 3);
        int height = sqlite3_column_int(stmt, 4);
        int64_t size = sqlite3_column_int64(stmt, 5);

        if (path == NULL || embedding_blob == NULL) {
            continue;
        }

        // Deserialize embedding
        if (blob_size != (int)(CLIP_EMBEDDING_DIMENSION * sizeof(float))) {
            continue;
        }

        const float *img_embedding = (const float *)embedding_blob;

        // Compute similarity
        float score = clip_similarity(img_embedding, text_result.embedding);

        // Filter by minimum score
        if (score < opts.min_score) {
            continue;
        }

        // Add to results
        if (results.count >= results.capacity) {
            // Expand capacity
            int new_capacity = results.capacity * 2;
            VisualSearchResult *new_results = realloc(results.results,
                                                       (size_t)new_capacity * sizeof(VisualSearchResult));
            if (new_results == NULL) {
                break;
            }
            results.results = new_results;
            results.capacity = new_capacity;
        }

        VisualSearchResult *r = &results.results[results.count];
        strncpy(r->path, path, sizeof(r->path) - 1);
        strncpy(r->name, name, sizeof(r->name) - 1);
        r->score = score;
        r->width = width;
        r->height = height;
        r->size = size;
        results.count++;
    }

    sqlite3_finalize(stmt);

    // Sort by score
    if (results.count > 1) {
        qsort(results.results, (size_t)results.count, sizeof(VisualSearchResult),
              compare_results_by_score);
    }

    // Limit to max_results
    if (opts.max_results > 0 && results.count > opts.max_results) {
        results.count = opts.max_results;
    }

    strncpy(results.query, query, sizeof(results.query) - 1);
    results.search_time_ms = get_time_ms() - start_time;
    results.success = true;

    return results;
}

VisualSearchResults visual_search_similar(VisualSearch *vs,
                                           const char *image_path,
                                           const VisualSearchOptions *options)
{
    if (vs == NULL || !vs->initialized) {
        return create_error_result("Visual search not initialized");
    }

    if (vs->clip_engine == NULL || !clip_engine_is_loaded(vs->clip_engine)) {
        return create_error_result("CLIP engine not ready");
    }

    if (image_path == NULL) {
        return create_error_result("Image path is NULL");
    }

    // Get embedding for the query image
    CLIPImageResult img_result = clip_embed_image(vs->clip_engine, image_path);
    if (img_result.status != CLIP_STATUS_OK) {
        return create_error_result(clip_status_message(img_result.status));
    }

    // Use default options if not provided
    VisualSearchOptions opts;
    if (options != NULL) {
        opts = *options;
    } else {
        opts = visual_search_default_options();
    }

    float start_time = get_time_ms();

    // Query all images from database
    sqlite3_stmt *stmt;
    const char *sql = (opts.directory != NULL) ? SQL_GET_IMAGES_IN_DIR : SQL_GET_ALL_IMAGES;

    int rc = sqlite3_prepare_v2(vs->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return create_error_result("Database query error");
    }

    if (opts.directory != NULL) {
        sqlite3_bind_text(stmt, 1, opts.directory, -1, SQLITE_STATIC);
    }

    // Allocate results
    int capacity = opts.max_results > 0 ? opts.max_results * 2 : 100;
    VisualSearchResults results = {0};
    results.results = calloc((size_t)capacity, sizeof(VisualSearchResult));
    if (results.results == NULL) {
        sqlite3_finalize(stmt);
        return create_error_result("Memory allocation error");
    }
    results.capacity = capacity;

    // Iterate and compute similarities
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *path = (const char *)sqlite3_column_text(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const void *embedding_blob = sqlite3_column_blob(stmt, 2);
        int blob_size = sqlite3_column_bytes(stmt, 2);
        int width = sqlite3_column_int(stmt, 3);
        int height = sqlite3_column_int(stmt, 4);
        int64_t size = sqlite3_column_int64(stmt, 5);

        if (path == NULL || embedding_blob == NULL) {
            continue;
        }

        // Skip the query image itself
        if (strcmp(path, image_path) == 0) {
            continue;
        }

        if (blob_size != (int)(CLIP_EMBEDDING_DIMENSION * sizeof(float))) {
            continue;
        }

        const float *db_embedding = (const float *)embedding_blob;
        float score = clip_similarity(db_embedding, img_result.embedding);

        if (score < opts.min_score) {
            continue;
        }

        // Add to results
        if (results.count >= results.capacity) {
            int new_capacity = results.capacity * 2;
            VisualSearchResult *new_results = realloc(results.results,
                                                       (size_t)new_capacity * sizeof(VisualSearchResult));
            if (new_results == NULL) {
                break;
            }
            results.results = new_results;
            results.capacity = new_capacity;
        }

        VisualSearchResult *r = &results.results[results.count];
        strncpy(r->path, path, sizeof(r->path) - 1);
        strncpy(r->name, name, sizeof(r->name) - 1);
        r->score = score;
        r->width = width;
        r->height = height;
        r->size = size;
        results.count++;
    }

    sqlite3_finalize(stmt);

    // Sort by score
    if (results.count > 1) {
        qsort(results.results, (size_t)results.count, sizeof(VisualSearchResult),
              compare_results_by_score);
    }

    // Limit results
    if (opts.max_results > 0 && results.count > opts.max_results) {
        results.count = opts.max_results;
    }

    snprintf(results.query, sizeof(results.query), "similar:%s", path_basename(image_path));
    results.search_time_ms = get_time_ms() - start_time;
    results.success = true;

    return results;
}

bool visual_search_index_image(VisualSearch *vs, const char *image_path)
{
    if (vs == NULL || !vs->initialized) {
        return false;
    }

    if (vs->clip_engine == NULL || !clip_engine_is_loaded(vs->clip_engine)) {
        return false;
    }

    if (vs->db == NULL) {
        return false;
    }

    if (image_path == NULL) {
        return false;
    }

    if (!clip_is_supported_image(image_path)) {
        return false;
    }

    // Get file info
    struct stat st;
    if (stat(image_path, &st) != 0) {
        return false;
    }

    // Check if already indexed with same modified time
    sqlite3_stmt *check_stmt;
    int rc = sqlite3_prepare_v2(vs->db, SQL_IS_IMAGE_INDEXED, -1, &check_stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(check_stmt, 1, image_path, -1, SQLITE_STATIC);
        sqlite3_bind_int64(check_stmt, 2, (sqlite3_int64)st.st_mtime);

        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            sqlite3_finalize(check_stmt);
            return true;  // Already indexed
        }
        sqlite3_finalize(check_stmt);
    }

    // Generate embedding
    CLIPImageResult img_result = clip_embed_image(vs->clip_engine, image_path);
    if (img_result.status != CLIP_STATUS_OK) {
        return false;
    }

    // Insert into database
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(vs->db, SQL_INSERT_IMAGE, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return false;
    }

    const char *name = path_basename(image_path);

    sqlite3_bind_text(stmt, 1, image_path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 3, img_result.embedding,
                      (int)(CLIP_EMBEDDING_DIMENSION * sizeof(float)), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, img_result.width);
    sqlite3_bind_int(stmt, 5, img_result.height);
    sqlite3_bind_int64(stmt, 6, (sqlite3_int64)st.st_size);
    sqlite3_bind_int64(stmt, 7, (sqlite3_int64)st.st_mtime);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

int visual_search_index_directory(VisualSearch *vs, const char *directory)
{
    if (vs == NULL || directory == NULL) {
        return 0;
    }

    DIR *dir = opendir(directory);
    if (dir == NULL) {
        return 0;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files and special directories
        if (entry->d_name[0] == '.') {
            continue;
        }

        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Recurse into subdirectory
            count += visual_search_index_directory(vs, path);
        } else if (S_ISREG(st.st_mode)) {
            // Try to index if it's an image
            if (visual_search_index_image(vs, path)) {
                count++;
            }
        }
    }

    closedir(dir);
    return count;
}

void visual_search_results_free(VisualSearchResults *results)
{
    if (results == NULL) {
        return;
    }

    if (results->results != NULL) {
        free(results->results);
        results->results = NULL;
    }
    results->count = 0;
    results->capacity = 0;
}

bool visual_search_is_ready(const VisualSearch *vs)
{
    if (vs == NULL || !vs->initialized) {
        return false;
    }

    if (vs->clip_engine == NULL || !clip_engine_is_loaded(vs->clip_engine)) {
        return false;
    }

    if (vs->db == NULL) {
        return false;
    }

    return true;
}

VisualSearchStats visual_search_get_stats(const VisualSearch *vs)
{
    VisualSearchStats stats = {0};

    if (vs == NULL || !vs->initialized) {
        return stats;
    }

    if (vs->clip_engine != NULL) {
        stats.engine_loaded = clip_engine_is_loaded(vs->clip_engine);
    }

    if (vs->db != NULL) {
        sqlite3_stmt *stmt;
        int rc = sqlite3_prepare_v2(vs->db, SQL_COUNT_IMAGES, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                stats.indexed_images = sqlite3_column_int64(stmt, 0);
                stats.total_images = stats.indexed_images;  // Approximation
            }
            sqlite3_finalize(stmt);
        }
    }

    return stats;
}

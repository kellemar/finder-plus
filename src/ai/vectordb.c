#include "vectordb.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

// VectorDB internal structure
struct VectorDB {
    sqlite3 *db;
    char db_path[4096];
    bool initialized;

    // Prepared statements for performance
    sqlite3_stmt *stmt_insert;
    sqlite3_stmt *stmt_update_embedding;
    sqlite3_stmt *stmt_delete;
    sqlite3_stmt *stmt_get_by_path;
    sqlite3_stmt *stmt_check_indexed;
    sqlite3_stmt *stmt_get_all_embeddings;
};

// SQL statements
static const char *SQL_CREATE_TABLE =
    "CREATE TABLE IF NOT EXISTS indexed_files ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  path TEXT UNIQUE NOT NULL,"
    "  name TEXT NOT NULL,"
    "  file_type INTEGER NOT NULL,"
    "  size INTEGER NOT NULL,"
    "  modified_time INTEGER NOT NULL,"
    "  indexed_time INTEGER NOT NULL,"
    "  embedding BLOB"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_path ON indexed_files(path);"
    "CREATE INDEX IF NOT EXISTS idx_modified ON indexed_files(modified_time);";

static const char *SQL_INSERT =
    "INSERT OR REPLACE INTO indexed_files "
    "(path, name, file_type, size, modified_time, indexed_time, embedding) "
    "VALUES (?, ?, ?, ?, ?, ?, ?);";

static const char *SQL_UPDATE_EMBEDDING =
    "UPDATE indexed_files SET embedding = ?, indexed_time = ? WHERE path = ?;";

static const char *SQL_DELETE =
    "DELETE FROM indexed_files WHERE path = ?;";

static const char *SQL_DELETE_DIR =
    "DELETE FROM indexed_files WHERE path LIKE ? || '%';";

static const char *SQL_GET_BY_PATH =
    "SELECT id, path, name, file_type, size, modified_time, indexed_time, embedding "
    "FROM indexed_files WHERE path = ?;";

static const char *SQL_CHECK_INDEXED =
    "SELECT 1 FROM indexed_files WHERE path = ? AND modified_time >= ?;";

static const char *SQL_GET_ALL_EMBEDDINGS =
    "SELECT id, path, name, file_type, size, modified_time, indexed_time, embedding "
    "FROM indexed_files WHERE embedding IS NOT NULL;";

static const char *SQL_GET_DIR_EMBEDDINGS =
    "SELECT id, path, name, file_type, size, modified_time, indexed_time, embedding "
    "FROM indexed_files WHERE embedding IS NOT NULL AND path LIKE ? || '%';";

static const char *SQL_COUNT =
    "SELECT COUNT(*) FROM indexed_files;";

static const char *SQL_TOTAL_SIZE =
    "SELECT COALESCE(SUM(size), 0) FROM indexed_files;";

static const char *SQL_CLEAR =
    "DELETE FROM indexed_files;";

// Schema version table
static const char *SQL_CREATE_VERSION_TABLE =
    "CREATE TABLE IF NOT EXISTS schema_version ("
    "  version INTEGER PRIMARY KEY"
    ");";

static const char *SQL_GET_VERSION =
    "SELECT COALESCE(MAX(version), 0) FROM schema_version;";

static const char *SQL_SET_VERSION =
    "INSERT OR REPLACE INTO schema_version (version) VALUES (?);";

// Current schema version
#define CURRENT_SCHEMA_VERSION 2

// Migration 1: Initial schema (already applied if table exists)
// Migration 2: Add content_hash column for duplicate detection
static const char *MIGRATION_2 =
    "ALTER TABLE indexed_files ADD COLUMN content_hash TEXT;"
    "CREATE INDEX IF NOT EXISTS idx_content_hash ON indexed_files(content_hash);";

// Helper: deserialize embedding from blob
static bool deserialize_embedding(const void *blob, int blob_size, float *output)
{
    if (blob == NULL || output == NULL) {
        return false;
    }

    size_t expected_size = EMBEDDING_DIMENSION * sizeof(float);
    if ((size_t)blob_size != expected_size) {
        return false;
    }

    memcpy(output, blob, expected_size);
    return true;
}

// Helper: fill IndexedFile from statement
static void fill_indexed_file(sqlite3_stmt *stmt, IndexedFile *file)
{
    file->id = sqlite3_column_int64(stmt, 0);

    const char *path = (const char *)sqlite3_column_text(stmt, 1);
    if (path != NULL) {
        strncpy(file->path, path, sizeof(file->path) - 1);
    }

    const char *name = (const char *)sqlite3_column_text(stmt, 2);
    if (name != NULL) {
        strncpy(file->name, name, sizeof(file->name) - 1);
    }

    file->file_type = (IndexedFileType)sqlite3_column_int(stmt, 3);
    file->size = sqlite3_column_int64(stmt, 4);
    file->modified_time = sqlite3_column_int64(stmt, 5);
    file->indexed_time = sqlite3_column_int64(stmt, 6);

    const void *embedding_blob = sqlite3_column_blob(stmt, 7);
    int embedding_size = sqlite3_column_bytes(stmt, 7);

    file->has_embedding = deserialize_embedding(embedding_blob, embedding_size, file->embedding);
}

VectorDB* vectordb_open(const char *db_path)
{
    if (db_path == NULL) {
        return NULL;
    }

    VectorDB *db = calloc(1, sizeof(VectorDB));
    if (db == NULL) {
        return NULL;
    }

    strncpy(db->db_path, db_path, sizeof(db->db_path) - 1);

    int rc = sqlite3_open(db_path, &db->db);
    if (rc != SQLITE_OK) {
        free(db);
        return NULL;
    }

    // Enable WAL mode for better performance
    sqlite3_exec(db->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    // Initialize schema
    if (vectordb_init_schema(db) != VECTORDB_STATUS_OK) {
        vectordb_close(db);
        return NULL;
    }

    // Prepare statements
    sqlite3_prepare_v2(db->db, SQL_INSERT, -1, &db->stmt_insert, NULL);
    sqlite3_prepare_v2(db->db, SQL_UPDATE_EMBEDDING, -1, &db->stmt_update_embedding, NULL);
    sqlite3_prepare_v2(db->db, SQL_DELETE, -1, &db->stmt_delete, NULL);
    sqlite3_prepare_v2(db->db, SQL_GET_BY_PATH, -1, &db->stmt_get_by_path, NULL);
    sqlite3_prepare_v2(db->db, SQL_CHECK_INDEXED, -1, &db->stmt_check_indexed, NULL);
    sqlite3_prepare_v2(db->db, SQL_GET_ALL_EMBEDDINGS, -1, &db->stmt_get_all_embeddings, NULL);

    db->initialized = true;
    return db;
}

void vectordb_close(VectorDB *db)
{
    if (db == NULL) {
        return;
    }

    // Finalize prepared statements
    if (db->stmt_insert) sqlite3_finalize(db->stmt_insert);
    if (db->stmt_update_embedding) sqlite3_finalize(db->stmt_update_embedding);
    if (db->stmt_delete) sqlite3_finalize(db->stmt_delete);
    if (db->stmt_get_by_path) sqlite3_finalize(db->stmt_get_by_path);
    if (db->stmt_check_indexed) sqlite3_finalize(db->stmt_check_indexed);
    if (db->stmt_get_all_embeddings) sqlite3_finalize(db->stmt_get_all_embeddings);

    if (db->db) {
        sqlite3_close(db->db);
    }

    free(db);
}

VectorDBStatus vectordb_init_schema(VectorDB *db)
{
    if (db == NULL || db->db == NULL) {
        return VECTORDB_STATUS_NOT_INITIALIZED;
    }

    char *err_msg = NULL;

    // Create version table first
    int rc = sqlite3_exec(db->db, SQL_CREATE_VERSION_TABLE, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
        return VECTORDB_STATUS_DB_ERROR;
    }

    // Create main table
    rc = sqlite3_exec(db->db, SQL_CREATE_TABLE, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
        return VECTORDB_STATUS_DB_ERROR;
    }

    // Run migrations
    return vectordb_migrate(db);
}

int vectordb_get_version(VectorDB *db)
{
    if (db == NULL || db->db == NULL) {
        return -1;
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, SQL_GET_VERSION, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return version;
}

static VectorDBStatus set_version(VectorDB *db, int version)
{
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, SQL_SET_VERSION, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return VECTORDB_STATUS_DB_ERROR;
    }

    sqlite3_bind_int(stmt, 1, version);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? VECTORDB_STATUS_OK : VECTORDB_STATUS_DB_ERROR;
}

VectorDBStatus vectordb_migrate(VectorDB *db)
{
    if (db == NULL || db->db == NULL) {
        return VECTORDB_STATUS_NOT_INITIALIZED;
    }

    int current_version = vectordb_get_version(db);
    if (current_version < 0) {
        return VECTORDB_STATUS_DB_ERROR;
    }

    // If this is a fresh database, set version to current
    if (current_version == 0) {
        return set_version(db, CURRENT_SCHEMA_VERSION);
    }

    char *err_msg = NULL;

    // Apply migrations
    if (current_version < 2) {
        // Try to apply migration 2 (add content_hash column)
        // SQLite's ALTER TABLE will error if column exists; we ignore the error
        sqlite3_exec(db->db, MIGRATION_2, NULL, NULL, &err_msg);
        if (err_msg) {
            sqlite3_free(err_msg);
            err_msg = NULL;
        }
    }

    // Update to current version
    return set_version(db, CURRENT_SCHEMA_VERSION);
}

VectorDBStatus vectordb_index_file(VectorDB *db,
                                    const char *path,
                                    const char *name,
                                    IndexedFileType file_type,
                                    int64_t size,
                                    int64_t modified_time,
                                    const float *embedding)
{
    if (db == NULL || !db->initialized) {
        return VECTORDB_STATUS_NOT_INITIALIZED;
    }

    if (path == NULL || name == NULL) {
        return VECTORDB_STATUS_INVALID_EMBEDDING;
    }

    sqlite3_reset(db->stmt_insert);

    sqlite3_bind_text(db->stmt_insert, 1, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(db->stmt_insert, 2, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(db->stmt_insert, 3, (int)file_type);
    sqlite3_bind_int64(db->stmt_insert, 4, size);
    sqlite3_bind_int64(db->stmt_insert, 5, modified_time);
    sqlite3_bind_int64(db->stmt_insert, 6, time(NULL));

    if (embedding != NULL) {
        sqlite3_bind_blob(db->stmt_insert, 7, embedding,
                          EMBEDDING_DIMENSION * sizeof(float), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(db->stmt_insert, 7);
    }

    int rc = sqlite3_step(db->stmt_insert);
    if (rc != SQLITE_DONE) {
        return VECTORDB_STATUS_DB_ERROR;
    }

    return VECTORDB_STATUS_OK;
}

VectorDBStatus vectordb_update_embedding(VectorDB *db,
                                          const char *path,
                                          const float *embedding)
{
    if (db == NULL || !db->initialized) {
        return VECTORDB_STATUS_NOT_INITIALIZED;
    }

    if (path == NULL || embedding == NULL) {
        return VECTORDB_STATUS_INVALID_EMBEDDING;
    }

    sqlite3_reset(db->stmt_update_embedding);

    sqlite3_bind_blob(db->stmt_update_embedding, 1, embedding,
                      EMBEDDING_DIMENSION * sizeof(float), SQLITE_TRANSIENT);
    sqlite3_bind_int64(db->stmt_update_embedding, 2, time(NULL));
    sqlite3_bind_text(db->stmt_update_embedding, 3, path, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(db->stmt_update_embedding);
    if (rc != SQLITE_DONE) {
        return VECTORDB_STATUS_DB_ERROR;
    }

    if (sqlite3_changes(db->db) == 0) {
        return VECTORDB_STATUS_NOT_FOUND;
    }

    return VECTORDB_STATUS_OK;
}

VectorDBStatus vectordb_delete_file(VectorDB *db, const char *path)
{
    if (db == NULL || !db->initialized) {
        return VECTORDB_STATUS_NOT_INITIALIZED;
    }

    if (path == NULL) {
        return VECTORDB_STATUS_NOT_FOUND;
    }

    sqlite3_reset(db->stmt_delete);
    sqlite3_bind_text(db->stmt_delete, 1, path, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(db->stmt_delete);
    if (rc != SQLITE_DONE) {
        return VECTORDB_STATUS_DB_ERROR;
    }

    return VECTORDB_STATUS_OK;
}

VectorDBStatus vectordb_delete_directory(VectorDB *db, const char *dir_path)
{
    if (db == NULL || !db->initialized) {
        return VECTORDB_STATUS_NOT_INITIALIZED;
    }

    if (dir_path == NULL) {
        return VECTORDB_STATUS_NOT_FOUND;
    }

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db->db, SQL_DELETE_DIR, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, dir_path, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return VECTORDB_STATUS_DB_ERROR;
    }

    return VECTORDB_STATUS_OK;
}

bool vectordb_is_indexed(VectorDB *db, const char *path, int64_t modified_time)
{
    if (db == NULL || !db->initialized || path == NULL) {
        return false;
    }

    sqlite3_reset(db->stmt_check_indexed);
    sqlite3_bind_text(db->stmt_check_indexed, 1, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(db->stmt_check_indexed, 2, modified_time);

    int rc = sqlite3_step(db->stmt_check_indexed);
    return rc == SQLITE_ROW;
}

VectorDBStatus vectordb_get_file(VectorDB *db, const char *path, IndexedFile *file)
{
    if (db == NULL || !db->initialized) {
        return VECTORDB_STATUS_NOT_INITIALIZED;
    }

    if (path == NULL || file == NULL) {
        return VECTORDB_STATUS_NOT_FOUND;
    }

    memset(file, 0, sizeof(IndexedFile));

    sqlite3_reset(db->stmt_get_by_path);
    sqlite3_bind_text(db->stmt_get_by_path, 1, path, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(db->stmt_get_by_path);
    if (rc != SQLITE_ROW) {
        return VECTORDB_STATUS_NOT_FOUND;
    }

    fill_indexed_file(db->stmt_get_by_path, file);
    return VECTORDB_STATUS_OK;
}

// Comparison function for sorting search results by similarity (descending)
static int compare_search_results(const void *a, const void *b)
{
    const VectorSearchResult *ra = (const VectorSearchResult *)a;
    const VectorSearchResult *rb = (const VectorSearchResult *)b;

    if (rb->similarity > ra->similarity) return 1;
    if (rb->similarity < ra->similarity) return -1;
    return 0;
}

VectorSearchResults vectordb_search(VectorDB *db,
                                     const float *query_embedding,
                                     int limit)
{
    VectorSearchResults results = {0};

    if (db == NULL || !db->initialized) {
        results.status = VECTORDB_STATUS_NOT_INITIALIZED;
        return results;
    }

    if (query_embedding == NULL || limit <= 0) {
        results.status = VECTORDB_STATUS_INVALID_EMBEDDING;
        return results;
    }

    if (limit > VECTORDB_MAX_RESULTS) {
        limit = VECTORDB_MAX_RESULTS;
    }

    // Allocate results array
    results.capacity = limit;
    results.results = calloc((size_t)limit, sizeof(VectorSearchResult));
    if (results.results == NULL) {
        results.status = VECTORDB_STATUS_MEMORY_ERROR;
        return results;
    }

    // Brute-force search through all embeddings
    sqlite3_reset(db->stmt_get_all_embeddings);

    int collected = 0;
    float min_similarity = 0.0f;

    while (sqlite3_step(db->stmt_get_all_embeddings) == SQLITE_ROW) {
        const void *blob = sqlite3_column_blob(db->stmt_get_all_embeddings, 7);
        int blob_size = sqlite3_column_bytes(db->stmt_get_all_embeddings, 7);

        float file_embedding[EMBEDDING_DIMENSION];
        if (!deserialize_embedding(blob, blob_size, file_embedding)) {
            continue;
        }

        float similarity = embedding_cosine_similarity(query_embedding, file_embedding);

        // If we haven't filled the results yet, or this is better than the worst
        if (collected < limit || similarity > min_similarity) {
            int insert_index;

            if (collected < limit) {
                insert_index = collected;
                collected++;
            } else {
                // Find the index with minimum similarity
                insert_index = 0;
                for (int i = 1; i < collected; i++) {
                    if (results.results[i].similarity < results.results[insert_index].similarity) {
                        insert_index = i;
                    }
                }
            }

            fill_indexed_file(db->stmt_get_all_embeddings, &results.results[insert_index].file);
            results.results[insert_index].similarity = similarity;

            // Update minimum similarity
            min_similarity = results.results[0].similarity;
            for (int i = 1; i < collected; i++) {
                if (results.results[i].similarity < min_similarity) {
                    min_similarity = results.results[i].similarity;
                }
            }
        }
    }

    results.count = collected;

    // Sort by similarity (descending)
    if (collected > 1) {
        qsort(results.results, (size_t)collected, sizeof(VectorSearchResult), compare_search_results);
    }

    results.status = VECTORDB_STATUS_OK;
    return results;
}

VectorSearchResults vectordb_search_in_directory(VectorDB *db,
                                                   const float *query_embedding,
                                                   const char *directory,
                                                   int limit)
{
    VectorSearchResults results = {0};

    if (db == NULL || !db->initialized) {
        results.status = VECTORDB_STATUS_NOT_INITIALIZED;
        return results;
    }

    if (query_embedding == NULL || directory == NULL || limit <= 0) {
        results.status = VECTORDB_STATUS_INVALID_EMBEDDING;
        return results;
    }

    if (limit > VECTORDB_MAX_RESULTS) {
        limit = VECTORDB_MAX_RESULTS;
    }

    // Allocate results array
    results.capacity = limit;
    results.results = calloc((size_t)limit, sizeof(VectorSearchResult));
    if (results.results == NULL) {
        results.status = VECTORDB_STATUS_MEMORY_ERROR;
        return results;
    }

    // Search within directory
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db->db, SQL_GET_DIR_EMBEDDINGS, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, directory, -1, SQLITE_TRANSIENT);

    int collected = 0;
    float min_similarity = 0.0f;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const void *blob = sqlite3_column_blob(stmt, 7);
        int blob_size = sqlite3_column_bytes(stmt, 7);

        float file_embedding[EMBEDDING_DIMENSION];
        if (!deserialize_embedding(blob, blob_size, file_embedding)) {
            continue;
        }

        float similarity = embedding_cosine_similarity(query_embedding, file_embedding);

        if (collected < limit || similarity > min_similarity) {
            int insert_index;

            if (collected < limit) {
                insert_index = collected;
                collected++;
            } else {
                insert_index = 0;
                for (int i = 1; i < collected; i++) {
                    if (results.results[i].similarity < results.results[insert_index].similarity) {
                        insert_index = i;
                    }
                }
            }

            fill_indexed_file(stmt, &results.results[insert_index].file);
            results.results[insert_index].similarity = similarity;

            min_similarity = results.results[0].similarity;
            for (int i = 1; i < collected; i++) {
                if (results.results[i].similarity < min_similarity) {
                    min_similarity = results.results[i].similarity;
                }
            }
        }
    }

    sqlite3_finalize(stmt);

    results.count = collected;

    if (collected > 1) {
        qsort(results.results, (size_t)collected, sizeof(VectorSearchResult), compare_search_results);
    }

    results.status = VECTORDB_STATUS_OK;
    return results;
}

void vector_search_results_free(VectorSearchResults *results)
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

int64_t vectordb_count_files(VectorDB *db)
{
    if (db == NULL || !db->initialized) {
        return 0;
    }

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db->db, SQL_COUNT, -1, &stmt, NULL);

    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

int64_t vectordb_total_size(VectorDB *db)
{
    if (db == NULL || !db->initialized) {
        return 0;
    }

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db->db, SQL_TOTAL_SIZE, -1, &stmt, NULL);

    int64_t total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return total;
}

VectorDBStatus vectordb_clear(VectorDB *db)
{
    if (db == NULL || !db->initialized) {
        return VECTORDB_STATUS_NOT_INITIALIZED;
    }

    int rc = sqlite3_exec(db->db, SQL_CLEAR, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        return VECTORDB_STATUS_DB_ERROR;
    }

    return VECTORDB_STATUS_OK;
}

const char* vectordb_status_message(VectorDBStatus status)
{
    switch (status) {
        case VECTORDB_STATUS_OK:
            return "Success";
        case VECTORDB_STATUS_NOT_INITIALIZED:
            return "Database not initialized";
        case VECTORDB_STATUS_DB_ERROR:
            return "Database error";
        case VECTORDB_STATUS_NOT_FOUND:
            return "File not found";
        case VECTORDB_STATUS_ALREADY_EXISTS:
            return "File already exists";
        case VECTORDB_STATUS_MEMORY_ERROR:
            return "Memory allocation error";
        case VECTORDB_STATUS_INVALID_EMBEDDING:
            return "Invalid embedding";
        default:
            return "Unknown error";
    }
}

// Extension to file type lookup table
typedef struct {
    const char *ext;
    IndexedFileType type;
} ExtensionMapping;

static const ExtensionMapping EXTENSION_MAP[] = {
    // Text
    {"txt", FILE_TYPE_TEXT}, {"md", FILE_TYPE_TEXT}, {"rst", FILE_TYPE_TEXT}, {"org", FILE_TYPE_TEXT},
    // Code
    {"c", FILE_TYPE_CODE}, {"h", FILE_TYPE_CODE}, {"cpp", FILE_TYPE_CODE}, {"hpp", FILE_TYPE_CODE},
    {"py", FILE_TYPE_CODE}, {"js", FILE_TYPE_CODE}, {"ts", FILE_TYPE_CODE}, {"go", FILE_TYPE_CODE},
    {"rs", FILE_TYPE_CODE}, {"java", FILE_TYPE_CODE}, {"swift", FILE_TYPE_CODE}, {"rb", FILE_TYPE_CODE},
    {"sh", FILE_TYPE_CODE}, {"json", FILE_TYPE_CODE}, {"yaml", FILE_TYPE_CODE}, {"yml", FILE_TYPE_CODE},
    {"xml", FILE_TYPE_CODE}, {"html", FILE_TYPE_CODE}, {"css", FILE_TYPE_CODE},
    // Documents
    {"pdf", FILE_TYPE_DOCUMENT}, {"doc", FILE_TYPE_DOCUMENT}, {"docx", FILE_TYPE_DOCUMENT},
    {"odt", FILE_TYPE_DOCUMENT}, {"rtf", FILE_TYPE_DOCUMENT}, {"xls", FILE_TYPE_DOCUMENT},
    {"xlsx", FILE_TYPE_DOCUMENT}, {"ppt", FILE_TYPE_DOCUMENT}, {"pptx", FILE_TYPE_DOCUMENT},
    // Images
    {"jpg", FILE_TYPE_IMAGE}, {"jpeg", FILE_TYPE_IMAGE}, {"png", FILE_TYPE_IMAGE},
    {"gif", FILE_TYPE_IMAGE}, {"bmp", FILE_TYPE_IMAGE}, {"svg", FILE_TYPE_IMAGE},
    {"webp", FILE_TYPE_IMAGE}, {"ico", FILE_TYPE_IMAGE}, {"tiff", FILE_TYPE_IMAGE}, {"heic", FILE_TYPE_IMAGE},
    // Audio
    {"mp3", FILE_TYPE_AUDIO}, {"wav", FILE_TYPE_AUDIO}, {"flac", FILE_TYPE_AUDIO},
    {"aac", FILE_TYPE_AUDIO}, {"ogg", FILE_TYPE_AUDIO}, {"m4a", FILE_TYPE_AUDIO},
    // Video
    {"mp4", FILE_TYPE_VIDEO}, {"mkv", FILE_TYPE_VIDEO}, {"avi", FILE_TYPE_VIDEO},
    {"mov", FILE_TYPE_VIDEO}, {"webm", FILE_TYPE_VIDEO}, {"wmv", FILE_TYPE_VIDEO},
    // Archive
    {"zip", FILE_TYPE_ARCHIVE}, {"tar", FILE_TYPE_ARCHIVE}, {"gz", FILE_TYPE_ARCHIVE},
    {"bz2", FILE_TYPE_ARCHIVE}, {"xz", FILE_TYPE_ARCHIVE}, {"7z", FILE_TYPE_ARCHIVE},
    {"rar", FILE_TYPE_ARCHIVE}, {"dmg", FILE_TYPE_ARCHIVE},
    {NULL, FILE_TYPE_UNKNOWN}  // Sentinel
};

IndexedFileType vectordb_file_type_from_extension(const char *extension)
{
    if (extension == NULL || extension[0] == '\0') {
        return FILE_TYPE_UNKNOWN;
    }

    for (const ExtensionMapping *m = EXTENSION_MAP; m->ext != NULL; m++) {
        if (strcmp(extension, m->ext) == 0) {
            return m->type;
        }
    }

    return FILE_TYPE_UNKNOWN;
}

sqlite3* vectordb_get_db_handle(VectorDB *db)
{
    if (db == NULL) {
        return NULL;
    }
    return db->db;
}

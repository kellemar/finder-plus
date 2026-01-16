#ifndef VECTORDB_H
#define VECTORDB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sqlite3.h>
#include "embeddings.h"

// Maximum number of search results
#define VECTORDB_MAX_RESULTS 100

// VectorDB status codes
typedef enum VectorDBStatus {
    VECTORDB_STATUS_OK = 0,
    VECTORDB_STATUS_NOT_INITIALIZED,
    VECTORDB_STATUS_DB_ERROR,
    VECTORDB_STATUS_NOT_FOUND,
    VECTORDB_STATUS_ALREADY_EXISTS,
    VECTORDB_STATUS_MEMORY_ERROR,
    VECTORDB_STATUS_INVALID_EMBEDDING
} VectorDBStatus;

// File type for indexed files
typedef enum IndexedFileType {
    FILE_TYPE_UNKNOWN = 0,
    FILE_TYPE_TEXT,
    FILE_TYPE_CODE,
    FILE_TYPE_DOCUMENT,
    FILE_TYPE_IMAGE,
    FILE_TYPE_AUDIO,
    FILE_TYPE_VIDEO,
    FILE_TYPE_ARCHIVE
} IndexedFileType;

// Indexed file entry
typedef struct IndexedFile {
    int64_t id;                         // Database ID
    char path[4096];                    // Full file path
    char name[256];                     // File name
    IndexedFileType file_type;          // File type
    int64_t size;                       // File size in bytes
    int64_t modified_time;              // Last modified timestamp
    int64_t indexed_time;               // When this was indexed
    float embedding[EMBEDDING_DIMENSION]; // Embedding vector
    bool has_embedding;                 // Whether embedding is valid
} IndexedFile;

// Search result
typedef struct VectorSearchResult {
    IndexedFile file;
    float similarity;                   // Cosine similarity score (0-1)
} VectorSearchResult;

// Search results array
typedef struct VectorSearchResults {
    VectorSearchResult *results;
    int count;
    int capacity;
    VectorDBStatus status;
} VectorSearchResults;

// VectorDB context (opaque)
typedef struct VectorDB VectorDB;

// Create/open vector database at given path
VectorDB* vectordb_open(const char *db_path);

// Close database and free resources
void vectordb_close(VectorDB *db);

// Initialize database schema (called automatically by vectordb_open)
VectorDBStatus vectordb_init_schema(VectorDB *db);

// Get database schema version
int vectordb_get_version(VectorDB *db);

// Run migrations to update schema to latest version
VectorDBStatus vectordb_migrate(VectorDB *db);

// Index a file with its embedding
VectorDBStatus vectordb_index_file(VectorDB *db,
                                    const char *path,
                                    const char *name,
                                    IndexedFileType file_type,
                                    int64_t size,
                                    int64_t modified_time,
                                    const float *embedding);

// Update file embedding
VectorDBStatus vectordb_update_embedding(VectorDB *db,
                                          const char *path,
                                          const float *embedding);

// Delete file from index
VectorDBStatus vectordb_delete_file(VectorDB *db, const char *path);

// Delete all files under a directory path
VectorDBStatus vectordb_delete_directory(VectorDB *db, const char *dir_path);

// Check if file is already indexed and up-to-date
bool vectordb_is_indexed(VectorDB *db, const char *path, int64_t modified_time);

// Get indexed file by path
VectorDBStatus vectordb_get_file(VectorDB *db, const char *path, IndexedFile *file);

// Search for similar files using embedding (brute-force cosine similarity)
VectorSearchResults vectordb_search(VectorDB *db,
                                     const float *query_embedding,
                                     int limit);

// Search for similar files within a directory
VectorSearchResults vectordb_search_in_directory(VectorDB *db,
                                                   const float *query_embedding,
                                                   const char *directory,
                                                   int limit);

// Free search results
void vector_search_results_free(VectorSearchResults *results);

// Get number of indexed files
int64_t vectordb_count_files(VectorDB *db);

// Get total size of indexed files
int64_t vectordb_total_size(VectorDB *db);

// Clear all indexed files
VectorDBStatus vectordb_clear(VectorDB *db);

// Get status message
const char* vectordb_status_message(VectorDBStatus status);

// Get file type from extension
IndexedFileType vectordb_file_type_from_extension(const char *extension);

// Get raw SQLite database handle (for advanced operations)
sqlite3* vectordb_get_db_handle(VectorDB *db);

#endif // VECTORDB_H

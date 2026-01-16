#ifndef INDEXER_H
#define INDEXER_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "embeddings.h"
#include "vectordb.h"

// Maximum number of directories to watch
#define INDEXER_MAX_WATCH_DIRS 32

// Maximum exclude patterns
#define INDEXER_MAX_EXCLUDE_PATTERNS 64

// Indexer status
typedef enum IndexerStatus {
    INDEXER_STATUS_STOPPED = 0,
    INDEXER_STATUS_RUNNING,
    INDEXER_STATUS_PAUSED,
    INDEXER_STATUS_WATCHING,  // Initial scan complete, watching for changes
    INDEXER_STATUS_ERROR
} IndexerStatus;

// Indexer statistics
typedef struct IndexerStats {
    int64_t files_indexed;
    int64_t files_pending;
    int64_t files_skipped;
    int64_t total_bytes;
    float progress;             // 0.0 to 1.0
    double elapsed_time_sec;
    double avg_time_per_file_ms;
} IndexerStats;

// Indexer configuration
typedef struct IndexerConfig {
    char db_path[4096];                               // Path to vector database
    char watch_dirs[INDEXER_MAX_WATCH_DIRS][4096];    // Directories to index
    int watch_dir_count;
    char exclude_patterns[INDEXER_MAX_EXCLUDE_PATTERNS][256];  // Patterns to exclude
    int exclude_pattern_count;
    bool index_hidden_files;                          // Index hidden files
    bool recursive;                                   // Index subdirectories
    int max_file_size_mb;                             // Skip files larger than this
    int batch_size;                                   // Files per batch
    int delay_between_batches_ms;                     // Throttle indexing
    bool enable_fsevents;                             // Use FSEvents for real-time watching
} IndexerConfig;

// Progress callback for indexing status updates
typedef void (*IndexerProgressCallback)(int64_t files_indexed, int64_t files_total,
                                          float progress, void *user_data);

// Callback for indexer events
typedef void (*IndexerCallback)(const char *path, IndexerStatus status, void *user_data);

// Indexer context (opaque)
typedef struct Indexer Indexer;

// Create indexer with default configuration
Indexer* indexer_create(void);

// Create indexer with custom configuration
Indexer* indexer_create_with_config(const IndexerConfig *config);

// Destroy indexer and free resources
void indexer_destroy(Indexer *indexer);

// Set the embedding engine (must be called before start)
void indexer_set_embedding_engine(Indexer *indexer, EmbeddingEngine *engine);

// Set the vector database (must be called before start)
void indexer_set_vectordb(Indexer *indexer, VectorDB *db);

// Add directory to watch list
bool indexer_add_watch_dir(Indexer *indexer, const char *path);

// Remove directory from watch list
bool indexer_remove_watch_dir(Indexer *indexer, const char *path);

// Add exclude pattern (e.g., "node_modules", "*.log")
bool indexer_add_exclude_pattern(Indexer *indexer, const char *pattern);

// Set callback for indexer events
void indexer_set_callback(Indexer *indexer, IndexerCallback callback, void *user_data);

// Set progress callback for UI updates
void indexer_set_progress_callback(Indexer *indexer, IndexerProgressCallback callback, void *user_data);

// Start indexing (spawns background thread)
bool indexer_start(Indexer *indexer);

// Enable/disable real-time file watching (uses FSEvents on macOS)
bool indexer_enable_watching(Indexer *indexer, bool enable);

// Stop indexing
void indexer_stop(Indexer *indexer);

// Pause indexing (can be resumed)
void indexer_pause(Indexer *indexer);

// Resume indexing after pause
void indexer_resume(Indexer *indexer);

// Get current status
IndexerStatus indexer_get_status(const Indexer *indexer);

// Get statistics (requires lock, so not const-correct)
IndexerStats indexer_get_stats(Indexer *indexer);

// Force re-index a specific file
bool indexer_reindex_file(Indexer *indexer, const char *path);

// Force re-index a directory
bool indexer_reindex_directory(Indexer *indexer, const char *path);

// Check if a file should be indexed (based on exclude patterns)
bool indexer_should_index_file(const Indexer *indexer, const char *path);

// Get default indexer configuration
IndexerConfig indexer_get_default_config(void);

// Check if indexer is busy
bool indexer_is_busy(const Indexer *indexer);

// Wait for indexing to complete (blocking)
void indexer_wait(Indexer *indexer);

#endif // INDEXER_H

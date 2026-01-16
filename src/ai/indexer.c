#include "indexer.h"
#include "../platform/fsevents.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <time.h>
#include <unistd.h>

// Default exclude patterns
static const char *DEFAULT_EXCLUDE_PATTERNS[] = {
    "node_modules",
    ".git",
    ".svn",
    ".hg",
    "__pycache__",
    "*.pyc",
    "*.o",
    "*.a",
    "*.so",
    "*.dylib",
    ".DS_Store",
    "Thumbs.db",
    "*.log",
    "*.tmp",
    "*.swp",
    NULL
};

// File queue entry
typedef struct FileQueueEntry {
    char path[4096];
    struct FileQueueEntry *next;
} FileQueueEntry;

// Indexer internal structure
struct Indexer {
    IndexerConfig config;
    EmbeddingEngine *embedding_engine;
    VectorDB *vectordb;

    // Threading
    pthread_t worker_thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool thread_running;

    // Status
    IndexerStatus status;
    IndexerStats stats;

    // Callback
    IndexerCallback callback;
    void *callback_user_data;

    // Progress callback
    IndexerProgressCallback progress_callback;
    void *progress_user_data;

    // File queue
    FileQueueEntry *queue_head;
    FileQueueEntry *queue_tail;
    int queue_size;

    // For progress tracking
    int64_t total_files_to_index;
    bool initial_scan_complete;

    // FSEvents watcher
    FSEventsWatcher *watcher;
    bool watching_enabled;

    // Timing
    struct timespec start_time;
};

// Forward declarations
static void scan_directory(Indexer *indexer, const char *dir_path);
static void process_file(Indexer *indexer, const char *path);
static bool should_index_file(const Indexer *indexer, const char *path, struct stat *st);
static void enqueue_file(Indexer *indexer, const char *path);

// Helper: get current time in seconds
static double get_current_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// FSEvents callback - called when files change
static void fsevents_handler(const FSEvent *event, void *user_data)
{
    Indexer *indexer = (Indexer *)user_data;
    if (indexer == NULL || event == NULL) {
        return;
    }

    // Skip directories
    if (event->flags & FSEVENT_FLAG_IS_DIR) {
        return;
    }

    switch (event->type) {
        case FSEVENT_CREATED:
        case FSEVENT_MODIFIED:
            // Queue file for indexing
            enqueue_file(indexer, event->path);
            break;

        case FSEVENT_DELETED:
            // Remove from index
            if (indexer->vectordb != NULL) {
                vectordb_delete_file(indexer->vectordb, event->path);
            }
            break;

        case FSEVENT_RENAMED:
            // Handle rename: delete old, index new (if exists)
            if (indexer->vectordb != NULL) {
                struct stat st;
                if (stat(event->path, &st) == 0) {
                    // New path exists - reindex
                    enqueue_file(indexer, event->path);
                } else {
                    // File was renamed away - delete from index
                    vectordb_delete_file(indexer->vectordb, event->path);
                }
            }
            break;

        default:
            break;
    }
}

// Helper: update progress
static void update_progress(Indexer *indexer)
{
    if (indexer->progress_callback == NULL) {
        return;
    }

    float progress = 0.0f;
    if (indexer->total_files_to_index > 0) {
        progress = (float)indexer->stats.files_indexed / (float)indexer->total_files_to_index;
        if (progress > 1.0f) progress = 1.0f;
    }
    indexer->stats.progress = progress;

    indexer->progress_callback(
        indexer->stats.files_indexed,
        indexer->total_files_to_index,
        progress,
        indexer->progress_user_data
    );
}

// Helper: check if path matches any exclude pattern
static bool matches_exclude_pattern(const Indexer *indexer, const char *path)
{
    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;

    for (int i = 0; i < indexer->config.exclude_pattern_count; i++) {
        const char *pattern = indexer->config.exclude_patterns[i];

        // Check both full path and basename
        if (fnmatch(pattern, path, FNM_PATHNAME) == 0 ||
            fnmatch(pattern, basename, 0) == 0) {
            return true;
        }
    }

    return false;
}

// Helper: check if file should be indexed based on type and size
static bool should_index_file(const Indexer *indexer, const char *path, struct stat *st)
{
    // Skip directories (they're enumerated, not indexed)
    if (S_ISDIR(st->st_mode)) {
        return false;
    }

    // Skip non-regular files
    if (!S_ISREG(st->st_mode)) {
        return false;
    }

    // Skip hidden files if configured
    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    if (!indexer->config.index_hidden_files && basename[0] == '.') {
        return false;
    }

    // Skip files exceeding max size
    int64_t max_bytes = (int64_t)indexer->config.max_file_size_mb * 1024 * 1024;
    if (max_bytes > 0 && st->st_size > max_bytes) {
        return false;
    }

    // Skip excluded patterns
    if (matches_exclude_pattern(indexer, path)) {
        return false;
    }

    return true;
}

// Helper: read file content for embedding
static char* read_file_content(const char *path, size_t max_size)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    if ((size_t)size > max_size) {
        size = (long)max_size;
    }

    char *content = malloc((size_t)size + 1);
    if (content == NULL) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(content, 1, (size_t)size, f);
    content[read] = '\0';
    fclose(f);

    return content;
}

// Helper: enqueue a file for indexing
static void enqueue_file(Indexer *indexer, const char *path)
{
    FileQueueEntry *entry = malloc(sizeof(FileQueueEntry));
    if (entry == NULL) {
        return;
    }

    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->next = NULL;

    pthread_mutex_lock(&indexer->mutex);

    if (indexer->queue_tail) {
        indexer->queue_tail->next = entry;
    } else {
        indexer->queue_head = entry;
    }
    indexer->queue_tail = entry;
    indexer->queue_size++;
    indexer->stats.files_pending = indexer->queue_size;

    pthread_cond_signal(&indexer->cond);
    pthread_mutex_unlock(&indexer->mutex);
}

// Helper: dequeue a file for processing
static bool dequeue_file(Indexer *indexer, char *path, size_t path_size)
{
    pthread_mutex_lock(&indexer->mutex);

    while (indexer->queue_head == NULL && indexer->thread_running && indexer->status != INDEXER_STATUS_STOPPED) {
        pthread_cond_wait(&indexer->cond, &indexer->mutex);
    }

    if (indexer->queue_head == NULL) {
        pthread_mutex_unlock(&indexer->mutex);
        return false;
    }

    FileQueueEntry *entry = indexer->queue_head;
    indexer->queue_head = entry->next;
    if (indexer->queue_head == NULL) {
        indexer->queue_tail = NULL;
    }
    indexer->queue_size--;
    indexer->stats.files_pending = indexer->queue_size;

    strncpy(path, entry->path, path_size - 1);
    free(entry);

    pthread_mutex_unlock(&indexer->mutex);
    return true;
}

// Helper: scan directory and enqueue files
static void scan_directory(Indexer *indexer, const char *dir_path)
{
    if (indexer->status == INDEXER_STATUS_STOPPED) {
        return;
    }

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (indexer->status == INDEXER_STATUS_STOPPED) {
            break;
        }

        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Build full path
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        // Get file info
        struct stat st;
        if (lstat(full_path, &st) != 0) {
            continue;
        }

        // Handle directories
        if (S_ISDIR(st.st_mode)) {
            if (indexer->config.recursive && !matches_exclude_pattern(indexer, full_path)) {
                scan_directory(indexer, full_path);
            }
            continue;
        }

        // Check if file should be indexed
        if (should_index_file(indexer, full_path, &st)) {
            enqueue_file(indexer, full_path);
        }
    }

    closedir(dir);
}

// Helper: process a single file
static void process_file(Indexer *indexer, const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        pthread_mutex_lock(&indexer->mutex);
        indexer->stats.files_skipped++;
        pthread_mutex_unlock(&indexer->mutex);
        return;
    }

    // Check if already indexed and up-to-date
    if (vectordb_is_indexed(indexer->vectordb, path, st.st_mtime)) {
        pthread_mutex_lock(&indexer->mutex);
        indexer->stats.files_skipped++;
        pthread_mutex_unlock(&indexer->mutex);
        return;
    }

    // Get file info
    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;

    const char *ext = strrchr(basename, '.');
    ext = ext ? ext + 1 : "";

    IndexedFileType file_type = vectordb_file_type_from_extension(ext);

    // Read file content for embedding (text and code files only)
    float *embedding = NULL;
    float embedding_data[EMBEDDING_DIMENSION];

    if ((file_type == FILE_TYPE_TEXT || file_type == FILE_TYPE_CODE) &&
        indexer->embedding_engine != NULL &&
        embedding_engine_is_loaded(indexer->embedding_engine)) {

        char *content = read_file_content(path, EMBEDDING_MAX_TEXT_LEN);
        if (content != NULL) {
            EmbeddingResult result = embedding_generate(indexer->embedding_engine, content);
            if (result.status == EMBEDDING_STATUS_OK) {
                memcpy(embedding_data, result.embedding, sizeof(embedding_data));
                embedding = embedding_data;
            }
            free(content);
        }
    }

    // Index the file
    VectorDBStatus status = vectordb_index_file(
        indexer->vectordb,
        path,
        basename,
        file_type,
        st.st_size,
        st.st_mtime,
        embedding
    );

    pthread_mutex_lock(&indexer->mutex);
    if (status == VECTORDB_STATUS_OK) {
        indexer->stats.files_indexed++;
        indexer->stats.total_bytes += st.st_size;
    } else {
        indexer->stats.files_skipped++;
    }

    // Update timing stats
    double elapsed = get_current_time_sec() - (indexer->start_time.tv_sec + indexer->start_time.tv_nsec / 1e9);
    indexer->stats.elapsed_time_sec = elapsed;
    if (indexer->stats.files_indexed > 0) {
        indexer->stats.avg_time_per_file_ms = (elapsed * 1000.0) / indexer->stats.files_indexed;
    }

    pthread_mutex_unlock(&indexer->mutex);

    // Update progress
    update_progress(indexer);

    // Call callback if set
    if (indexer->callback) {
        indexer->callback(path, indexer->status, indexer->callback_user_data);
    }
}

// Worker thread function
static void* worker_thread_func(void *arg)
{
    Indexer *indexer = (Indexer *)arg;

    // Initial scan of all watch directories
    for (int i = 0; i < indexer->config.watch_dir_count; i++) {
        if (indexer->status == INDEXER_STATUS_STOPPED) {
            break;
        }
        scan_directory(indexer, indexer->config.watch_dirs[i]);
    }

    // Record total files for progress tracking
    pthread_mutex_lock(&indexer->mutex);
    indexer->total_files_to_index = indexer->queue_size;
    pthread_mutex_unlock(&indexer->mutex);

    // Process files from queue
    char path[4096];
    while (indexer->thread_running) {
        if (!dequeue_file(indexer, path, sizeof(path))) {
            // Queue is empty - initial scan complete
            pthread_mutex_lock(&indexer->mutex);
            if (!indexer->initial_scan_complete) {
                indexer->initial_scan_complete = true;

                // Start FSEvents watcher if enabled
                if (indexer->config.enable_fsevents && indexer->watcher != NULL) {
                    indexer->status = INDEXER_STATUS_WATCHING;
                    fsevents_start(indexer->watcher);
                }
            }
            pthread_mutex_unlock(&indexer->mutex);

            // Wait for more files (from FSEvents or reindex requests)
            pthread_mutex_lock(&indexer->mutex);
            while (indexer->queue_head == NULL && indexer->thread_running) {
                struct timespec timeout;
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 1;  // Check every second
                pthread_cond_timedwait(&indexer->cond, &indexer->mutex, &timeout);
            }
            pthread_mutex_unlock(&indexer->mutex);
            continue;
        }

        // Handle pause
        pthread_mutex_lock(&indexer->mutex);
        while (indexer->status == INDEXER_STATUS_PAUSED && indexer->thread_running) {
            pthread_cond_wait(&indexer->cond, &indexer->mutex);
        }
        pthread_mutex_unlock(&indexer->mutex);

        if (!indexer->thread_running) {
            break;
        }

        process_file(indexer, path);

        // Delay between batches if configured
        if (indexer->config.delay_between_batches_ms > 0) {
            usleep((useconds_t)indexer->config.delay_between_batches_ms * 1000);
        }
    }

    return NULL;
}

IndexerConfig indexer_get_default_config(void)
{
    IndexerConfig config = {0};

    strncpy(config.db_path, "finder_plus_index.db", sizeof(config.db_path) - 1);
    config.watch_dir_count = 0;
    config.index_hidden_files = false;
    config.recursive = true;
    config.max_file_size_mb = 10;  // Skip files > 10MB
    config.batch_size = 32;
    config.delay_between_batches_ms = 10;

    // Add default exclude patterns
    for (int i = 0; DEFAULT_EXCLUDE_PATTERNS[i] != NULL; i++) {
        if (config.exclude_pattern_count < INDEXER_MAX_EXCLUDE_PATTERNS) {
            strncpy(config.exclude_patterns[config.exclude_pattern_count],
                    DEFAULT_EXCLUDE_PATTERNS[i],
                    sizeof(config.exclude_patterns[0]) - 1);
            config.exclude_pattern_count++;
        }
    }

    return config;
}

Indexer* indexer_create(void)
{
    IndexerConfig config = indexer_get_default_config();
    return indexer_create_with_config(&config);
}

Indexer* indexer_create_with_config(const IndexerConfig *config)
{
    if (config == NULL) {
        return NULL;
    }

    Indexer *indexer = calloc(1, sizeof(Indexer));
    if (indexer == NULL) {
        return NULL;
    }

    memcpy(&indexer->config, config, sizeof(IndexerConfig));

    pthread_mutex_init(&indexer->mutex, NULL);
    pthread_cond_init(&indexer->cond, NULL);

    indexer->status = INDEXER_STATUS_STOPPED;
    indexer->thread_running = false;
    indexer->initial_scan_complete = false;

    // Create FSEvents watcher if enabled
    if (config->enable_fsevents) {
        indexer->watcher = fsevents_create();
        if (indexer->watcher != NULL) {
            fsevents_set_callback(indexer->watcher, fsevents_handler, indexer);
            fsevents_set_latency(indexer->watcher, 0.5);

            // Add watch directories
            for (int i = 0; i < config->watch_dir_count; i++) {
                fsevents_add_path(indexer->watcher, config->watch_dirs[i]);
            }
        }
    }

    return indexer;
}

void indexer_destroy(Indexer *indexer)
{
    if (indexer == NULL) {
        return;
    }

    indexer_stop(indexer);

    // Stop and destroy FSEvents watcher
    if (indexer->watcher != NULL) {
        fsevents_destroy(indexer->watcher);
        indexer->watcher = NULL;
    }

    // Free remaining queue entries
    FileQueueEntry *entry = indexer->queue_head;
    while (entry) {
        FileQueueEntry *next = entry->next;
        free(entry);
        entry = next;
    }

    pthread_mutex_destroy(&indexer->mutex);
    pthread_cond_destroy(&indexer->cond);

    free(indexer);
}

void indexer_set_embedding_engine(Indexer *indexer, EmbeddingEngine *engine)
{
    if (indexer == NULL) {
        return;
    }
    indexer->embedding_engine = engine;
}

void indexer_set_vectordb(Indexer *indexer, VectorDB *db)
{
    if (indexer == NULL) {
        return;
    }
    indexer->vectordb = db;
}

bool indexer_add_watch_dir(Indexer *indexer, const char *path)
{
    if (indexer == NULL || path == NULL) {
        return false;
    }

    pthread_mutex_lock(&indexer->mutex);

    if (indexer->config.watch_dir_count >= INDEXER_MAX_WATCH_DIRS) {
        pthread_mutex_unlock(&indexer->mutex);
        return false;
    }

    strncpy(indexer->config.watch_dirs[indexer->config.watch_dir_count],
            path, sizeof(indexer->config.watch_dirs[0]) - 1);
    indexer->config.watch_dir_count++;

    pthread_mutex_unlock(&indexer->mutex);
    return true;
}

bool indexer_remove_watch_dir(Indexer *indexer, const char *path)
{
    if (indexer == NULL || path == NULL) {
        return false;
    }

    pthread_mutex_lock(&indexer->mutex);

    for (int i = 0; i < indexer->config.watch_dir_count; i++) {
        if (strcmp(indexer->config.watch_dirs[i], path) == 0) {
            // Shift remaining entries
            for (int j = i; j < indexer->config.watch_dir_count - 1; j++) {
                strncpy(indexer->config.watch_dirs[j],
                        indexer->config.watch_dirs[j + 1],
                        sizeof(indexer->config.watch_dirs[0]));
            }
            indexer->config.watch_dir_count--;
            pthread_mutex_unlock(&indexer->mutex);
            return true;
        }
    }

    pthread_mutex_unlock(&indexer->mutex);
    return false;
}

bool indexer_add_exclude_pattern(Indexer *indexer, const char *pattern)
{
    if (indexer == NULL || pattern == NULL) {
        return false;
    }

    pthread_mutex_lock(&indexer->mutex);

    if (indexer->config.exclude_pattern_count >= INDEXER_MAX_EXCLUDE_PATTERNS) {
        pthread_mutex_unlock(&indexer->mutex);
        return false;
    }

    strncpy(indexer->config.exclude_patterns[indexer->config.exclude_pattern_count],
            pattern, sizeof(indexer->config.exclude_patterns[0]) - 1);
    indexer->config.exclude_pattern_count++;

    pthread_mutex_unlock(&indexer->mutex);
    return true;
}

void indexer_set_callback(Indexer *indexer, IndexerCallback callback, void *user_data)
{
    if (indexer == NULL) {
        return;
    }

    pthread_mutex_lock(&indexer->mutex);
    indexer->callback = callback;
    indexer->callback_user_data = user_data;
    pthread_mutex_unlock(&indexer->mutex);
}

void indexer_set_progress_callback(Indexer *indexer, IndexerProgressCallback callback, void *user_data)
{
    if (indexer == NULL) {
        return;
    }

    pthread_mutex_lock(&indexer->mutex);
    indexer->progress_callback = callback;
    indexer->progress_user_data = user_data;
    pthread_mutex_unlock(&indexer->mutex);
}

bool indexer_enable_watching(Indexer *indexer, bool enable)
{
    if (indexer == NULL) {
        return false;
    }

    pthread_mutex_lock(&indexer->mutex);

    // If we're already in the desired state, nothing to do
    if (indexer->watching_enabled == enable) {
        pthread_mutex_unlock(&indexer->mutex);
        return true;
    }

    if (enable) {
        // Create watcher if it doesn't exist
        if (indexer->watcher == NULL) {
            indexer->watcher = fsevents_create();
            if (indexer->watcher == NULL) {
                pthread_mutex_unlock(&indexer->mutex);
                return false;
            }
            fsevents_set_callback(indexer->watcher, fsevents_handler, indexer);
            fsevents_set_latency(indexer->watcher, 0.5);

            // Add watch directories
            for (int i = 0; i < indexer->config.watch_dir_count; i++) {
                fsevents_add_path(indexer->watcher, indexer->config.watch_dirs[i]);
            }
        }

        // Start watching if initial scan is complete
        if (indexer->initial_scan_complete) {
            if (!fsevents_start(indexer->watcher)) {
                pthread_mutex_unlock(&indexer->mutex);
                return false;
            }
            indexer->status = INDEXER_STATUS_WATCHING;
        }

        indexer->watching_enabled = true;
        indexer->config.enable_fsevents = true;
    } else {
        // Stop watching
        if (indexer->watcher != NULL && fsevents_is_running(indexer->watcher)) {
            fsevents_stop(indexer->watcher);
        }

        if (indexer->status == INDEXER_STATUS_WATCHING) {
            indexer->status = INDEXER_STATUS_RUNNING;
        }

        indexer->watching_enabled = false;
        indexer->config.enable_fsevents = false;
    }

    pthread_mutex_unlock(&indexer->mutex);
    return true;
}

bool indexer_start(Indexer *indexer)
{
    if (indexer == NULL) {
        return false;
    }

    if (indexer->vectordb == NULL) {
        return false;
    }

    pthread_mutex_lock(&indexer->mutex);

    if (indexer->thread_running) {
        pthread_mutex_unlock(&indexer->mutex);
        return false;
    }

    // Reset stats
    memset(&indexer->stats, 0, sizeof(IndexerStats));
    clock_gettime(CLOCK_MONOTONIC, &indexer->start_time);

    indexer->status = INDEXER_STATUS_RUNNING;
    indexer->thread_running = true;

    pthread_mutex_unlock(&indexer->mutex);

    if (pthread_create(&indexer->worker_thread, NULL, worker_thread_func, indexer) != 0) {
        pthread_mutex_lock(&indexer->mutex);
        indexer->status = INDEXER_STATUS_ERROR;
        indexer->thread_running = false;
        pthread_mutex_unlock(&indexer->mutex);
        return false;
    }

    return true;
}

void indexer_stop(Indexer *indexer)
{
    if (indexer == NULL) {
        return;
    }

    pthread_mutex_lock(&indexer->mutex);
    indexer->status = INDEXER_STATUS_STOPPED;
    indexer->thread_running = false;
    pthread_cond_broadcast(&indexer->cond);
    pthread_mutex_unlock(&indexer->mutex);

    // Stop FSEvents watcher
    if (indexer->watcher != NULL && fsevents_is_running(indexer->watcher)) {
        fsevents_stop(indexer->watcher);
    }

    if (indexer->worker_thread) {
        pthread_join(indexer->worker_thread, NULL);
        indexer->worker_thread = 0;
    }
}

void indexer_pause(Indexer *indexer)
{
    if (indexer == NULL) {
        return;
    }

    pthread_mutex_lock(&indexer->mutex);
    if (indexer->status == INDEXER_STATUS_RUNNING) {
        indexer->status = INDEXER_STATUS_PAUSED;
    }
    pthread_mutex_unlock(&indexer->mutex);
}

void indexer_resume(Indexer *indexer)
{
    if (indexer == NULL) {
        return;
    }

    pthread_mutex_lock(&indexer->mutex);
    if (indexer->status == INDEXER_STATUS_PAUSED) {
        indexer->status = INDEXER_STATUS_RUNNING;
        pthread_cond_broadcast(&indexer->cond);
    }
    pthread_mutex_unlock(&indexer->mutex);
}

IndexerStatus indexer_get_status(const Indexer *indexer)
{
    if (indexer == NULL) {
        return INDEXER_STATUS_ERROR;
    }
    return indexer->status;
}

IndexerStats indexer_get_stats(Indexer *indexer)
{
    IndexerStats stats = {0};

    if (indexer == NULL) {
        return stats;
    }

    pthread_mutex_lock(&indexer->mutex);
    memcpy(&stats, &indexer->stats, sizeof(IndexerStats));
    pthread_mutex_unlock(&indexer->mutex);

    return stats;
}

bool indexer_reindex_file(Indexer *indexer, const char *path)
{
    if (indexer == NULL || path == NULL) {
        return false;
    }

    // Delete existing entry and re-queue
    vectordb_delete_file(indexer->vectordb, path);
    enqueue_file(indexer, path);

    return true;
}

bool indexer_reindex_directory(Indexer *indexer, const char *path)
{
    if (indexer == NULL || path == NULL) {
        return false;
    }

    // Delete existing entries
    vectordb_delete_directory(indexer->vectordb, path);

    // Re-scan directory
    scan_directory(indexer, path);

    return true;
}

bool indexer_should_index_file(const Indexer *indexer, const char *path)
{
    if (indexer == NULL || path == NULL) {
        return false;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }

    return should_index_file(indexer, path, &st);
}

bool indexer_is_busy(const Indexer *indexer)
{
    if (indexer == NULL) {
        return false;
    }

    return indexer->status == INDEXER_STATUS_RUNNING && indexer->queue_size > 0;
}

void indexer_wait(Indexer *indexer)
{
    if (indexer == NULL || !indexer->thread_running) {
        return;
    }

    pthread_join(indexer->worker_thread, NULL);
}

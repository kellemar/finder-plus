#ifndef PERF_H
#define PERF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

//=============================================================================
// Directory Cache
//=============================================================================

#define DIR_CACHE_MAX_ENTRIES 32
#define DIR_CACHE_MAX_AGE 5.0  // Seconds before cache entry is stale

// Forward declaration
struct DirectoryState;

typedef struct DirCacheEntry {
    char path[4096];
    struct DirectoryState *directory;
    double timestamp;
    bool valid;
} DirCacheEntry;

typedef struct DirCache {
    DirCacheEntry entries[DIR_CACHE_MAX_ENTRIES];
    int count;
    bool enabled;
} DirCache;

// Initialize directory cache
void dir_cache_init(DirCache *cache);

// Free directory cache
void dir_cache_free(DirCache *cache);

// Get cached directory (returns NULL if not cached or stale)
struct DirectoryState* dir_cache_get(DirCache *cache, const char *path);

// Store directory in cache (makes a copy)
void dir_cache_put(DirCache *cache, const char *path, const struct DirectoryState *dir);

// Invalidate a specific path (and its children)
void dir_cache_invalidate(DirCache *cache, const char *path);

// Clear entire cache
void dir_cache_clear(DirCache *cache);

// Enable/disable caching
void dir_cache_set_enabled(DirCache *cache, bool enabled);

//=============================================================================
// Dirty Rectangle Tracking
//=============================================================================

#define MAX_DIRTY_RECTS 32

typedef struct DirtyRect {
    int x, y, width, height;
} DirtyRect;

typedef struct DirtyRectTracker {
    DirtyRect rects[MAX_DIRTY_RECTS];
    int count;
    bool full_redraw;
    bool enabled;
} DirtyRectTracker;

// Initialize dirty rect tracker
void dirty_init(DirtyRectTracker *tracker);

// Mark a rectangle as dirty
void dirty_add(DirtyRectTracker *tracker, int x, int y, int width, int height);

// Mark entire screen as dirty
void dirty_full(DirtyRectTracker *tracker);

// Clear dirty state (call after redraw)
void dirty_clear(DirtyRectTracker *tracker);

// Check if anything needs redrawing
bool dirty_needs_redraw(DirtyRectTracker *tracker);

// Check if a region intersects any dirty rect
bool dirty_intersects(DirtyRectTracker *tracker, int x, int y, int width, int height);

// Enable/disable dirty rect tracking
void dirty_set_enabled(DirtyRectTracker *tracker, bool enabled);

//=============================================================================
// Lazy Loading Queue
//=============================================================================

#define LAZY_QUEUE_MAX 256

typedef enum LazyTaskType {
    LAZY_TASK_THUMBNAIL,
    LAZY_TASK_FILE_INFO,
    LAZY_TASK_PREVIEW
} LazyTaskType;

typedef enum LazyTaskStatus {
    LAZY_STATUS_PENDING,
    LAZY_STATUS_IN_PROGRESS,
    LAZY_STATUS_COMPLETE,
    LAZY_STATUS_ERROR
} LazyTaskStatus;

typedef struct LazyTask {
    LazyTaskType type;
    LazyTaskStatus status;
    char path[4096];
    int priority;
    void *result;
    double start_time;
} LazyTask;

typedef struct LazyLoadQueue {
    LazyTask tasks[LAZY_QUEUE_MAX];
    int count;
    int processing;
    bool enabled;
} LazyLoadQueue;

// Initialize lazy load queue
void lazy_init(LazyLoadQueue *queue);

// Free lazy load queue
void lazy_free(LazyLoadQueue *queue);

// Add task to queue (returns task index)
int lazy_enqueue(LazyLoadQueue *queue, LazyTaskType type, const char *path, int priority);

// Cancel task by path
void lazy_cancel(LazyLoadQueue *queue, const char *path);

// Cancel all pending tasks
void lazy_cancel_all(LazyLoadQueue *queue);

// Process one pending task (call from main thread or worker)
bool lazy_process_one(LazyLoadQueue *queue);

// Get completed task result (returns NULL if not ready)
void* lazy_get_result(LazyLoadQueue *queue, const char *path, LazyTaskType type);

// Check if task is complete
bool lazy_is_complete(LazyLoadQueue *queue, const char *path, LazyTaskType type);

//=============================================================================
// Memory Profiling
//=============================================================================

typedef struct MemoryStats {
    size_t total_allocated;
    size_t peak_allocated;
    size_t allocation_count;
    size_t free_count;
    size_t directory_cache_bytes;
    size_t thumbnail_cache_bytes;
    size_t preview_bytes;
} MemoryStats;

// Global memory stats
extern MemoryStats g_memory_stats;

// Start memory profiling
void memory_profile_start(void);

// Stop memory profiling
void memory_profile_stop(void);

// Get current memory usage
void memory_profile_snapshot(MemoryStats *stats);

// Print memory report
void memory_profile_print(void);

// Track allocation (call from malloc wrapper)
void memory_track_alloc(size_t size);

// Track deallocation (call from free wrapper)
void memory_track_free(size_t size);

//=============================================================================
// Frame Timing
//=============================================================================

typedef struct FrameTimings {
    double frame_times[120];
    int frame_index;
    double min_frame_time;
    double max_frame_time;
    double avg_frame_time;
    double last_update;
    int frame_count;
} FrameTimings;

// Initialize frame timing
void timing_init(FrameTimings *timings);

// Record a frame
void timing_record_frame(FrameTimings *timings, double frame_time);

// Get average FPS
double timing_get_fps(FrameTimings *timings);

// Get frame time percentile
double timing_get_percentile(FrameTimings *timings, float percentile);

// Check if we're hitting target FPS
bool timing_hitting_target(FrameTimings *timings, double target_fps);

//=============================================================================
// Performance Manager (combines all systems)
//=============================================================================

typedef struct PerfManager {
    DirCache dir_cache;
    DirtyRectTracker dirty;
    LazyLoadQueue lazy_queue;
    FrameTimings timings;
    bool profiling_enabled;
} PerfManager;

// Initialize performance manager
void perf_init(PerfManager *perf);

// Free performance manager
void perf_free(PerfManager *perf);

// Update performance systems (call once per frame)
void perf_update(PerfManager *perf, double frame_time);

// Enable/disable all optimization systems
void perf_set_enabled(PerfManager *perf, bool enabled);

// Get current performance statistics string
void perf_get_stats_string(PerfManager *perf, char *buffer, size_t buffer_size);

#endif // PERF_H

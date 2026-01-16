#include "perf.h"
#include "../core/filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global memory stats
MemoryStats g_memory_stats = {0};

//=============================================================================
// Directory Cache Implementation
//=============================================================================

void dir_cache_init(DirCache *cache)
{
    memset(cache, 0, sizeof(DirCache));
    cache->enabled = true;
}

void dir_cache_free(DirCache *cache)
{
    dir_cache_clear(cache);
}

static double get_time_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int find_cache_entry(DirCache *cache, const char *path)
{
    for (int i = 0; i < cache->count; i++) {
        if (cache->entries[i].valid && strcmp(cache->entries[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_oldest_entry(DirCache *cache)
{
    if (cache->count == 0) return 0;  // Defensive check

    int oldest = 0;
    double oldest_time = cache->entries[0].timestamp;

    for (int i = 1; i < cache->count; i++) {
        if (cache->entries[i].timestamp < oldest_time) {
            oldest = i;
            oldest_time = cache->entries[i].timestamp;
        }
    }
    return oldest;
}

DirectoryState* dir_cache_get(DirCache *cache, const char *path)
{
    if (!cache->enabled) {
        return NULL;
    }

    int idx = find_cache_entry(cache, path);
    if (idx < 0) {
        return NULL;
    }

    DirCacheEntry *entry = &cache->entries[idx];

    // Check if entry is stale
    double now = get_time_seconds();
    if (now - entry->timestamp > DIR_CACHE_MAX_AGE) {
        // Invalidate stale entry
        if (entry->directory) {
            directory_state_free(entry->directory);
            free(entry->directory);
            entry->directory = NULL;
        }
        entry->valid = false;
        return NULL;
    }

    return entry->directory;
}

void dir_cache_put(DirCache *cache, const char *path, const DirectoryState *dir)
{
    if (!cache->enabled) {
        return;
    }

    // Check if already cached
    int idx = find_cache_entry(cache, path);

    if (idx < 0) {
        // Find slot for new entry
        if (cache->count < DIR_CACHE_MAX_ENTRIES) {
            idx = cache->count;
            cache->count++;
        } else {
            // Evict oldest entry
            idx = find_oldest_entry(cache);
            if (cache->entries[idx].directory) {
                directory_state_free(cache->entries[idx].directory);
                free(cache->entries[idx].directory);
            }
        }
    } else {
        // Update existing entry
        if (cache->entries[idx].directory) {
            directory_state_free(cache->entries[idx].directory);
            free(cache->entries[idx].directory);
        }
    }

    DirCacheEntry *entry = &cache->entries[idx];
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->path[sizeof(entry->path) - 1] = '\0';

    // Deep copy the directory state
    entry->directory = malloc(sizeof(DirectoryState));
    if (entry->directory) {
        directory_state_init(entry->directory);
        strncpy(entry->directory->current_path, dir->current_path, PATH_MAX_LEN - 1);
        entry->directory->show_hidden = dir->show_hidden;
        entry->directory->is_loading = false;

        if (dir->count > 0 && dir->entries) {
            entry->directory->entries = malloc(dir->count * sizeof(FileEntry));
            if (entry->directory->entries) {
                memcpy(entry->directory->entries, dir->entries, dir->count * sizeof(FileEntry));
                entry->directory->count = dir->count;
                entry->directory->capacity = dir->count;
            }
        }

        g_memory_stats.directory_cache_bytes += sizeof(DirectoryState) + dir->count * sizeof(FileEntry);
    }

    entry->timestamp = get_time_seconds();
    entry->valid = true;
}

void dir_cache_invalidate(DirCache *cache, const char *path)
{
    size_t path_len = strlen(path);

    for (int i = 0; i < cache->count; i++) {
        DirCacheEntry *entry = &cache->entries[i];
        if (!entry->valid) continue;

        // Invalidate if exact match or child path
        if (strcmp(entry->path, path) == 0 ||
            (strncmp(entry->path, path, path_len) == 0 && entry->path[path_len] == '/')) {
            if (entry->directory) {
                size_t freed = sizeof(DirectoryState) + entry->directory->count * sizeof(FileEntry);
                g_memory_stats.directory_cache_bytes -= freed;

                directory_state_free(entry->directory);
                free(entry->directory);
                entry->directory = NULL;
            }
            entry->valid = false;
        }
    }
}

void dir_cache_clear(DirCache *cache)
{
    for (int i = 0; i < cache->count; i++) {
        if (cache->entries[i].directory) {
            directory_state_free(cache->entries[i].directory);
            free(cache->entries[i].directory);
            cache->entries[i].directory = NULL;
        }
        cache->entries[i].valid = false;
    }
    cache->count = 0;
    g_memory_stats.directory_cache_bytes = 0;
}

void dir_cache_set_enabled(DirCache *cache, bool enabled)
{
    cache->enabled = enabled;
    if (!enabled) {
        dir_cache_clear(cache);
    }
}

//=============================================================================
// Dirty Rectangle Tracking Implementation
//=============================================================================

void dirty_init(DirtyRectTracker *tracker)
{
    memset(tracker, 0, sizeof(DirtyRectTracker));
    tracker->full_redraw = true;  // Start with full redraw
    tracker->enabled = true;
}

void dirty_add(DirtyRectTracker *tracker, int x, int y, int width, int height)
{
    if (!tracker->enabled || tracker->full_redraw) {
        return;
    }

    // Merge with existing rects if overlapping
    for (int i = 0; i < tracker->count; i++) {
        DirtyRect *r = &tracker->rects[i];

        // Check for overlap (with some margin for merging adjacent rects)
        int margin = 8;
        if (x <= r->x + r->width + margin && x + width >= r->x - margin &&
            y <= r->y + r->height + margin && y + height >= r->y - margin) {
            // Expand existing rect to include new area
            int new_x = (x < r->x) ? x : r->x;
            int new_y = (y < r->y) ? y : r->y;
            int new_right = ((x + width) > (r->x + r->width)) ? (x + width) : (r->x + r->width);
            int new_bottom = ((y + height) > (r->y + r->height)) ? (y + height) : (r->y + r->height);

            r->x = new_x;
            r->y = new_y;
            r->width = new_right - new_x;
            r->height = new_bottom - new_y;
            return;
        }
    }

    // Add new rect
    if (tracker->count < MAX_DIRTY_RECTS) {
        DirtyRect *r = &tracker->rects[tracker->count++];
        r->x = x;
        r->y = y;
        r->width = width;
        r->height = height;
    } else {
        // Too many rects, fall back to full redraw
        tracker->full_redraw = true;
    }
}

void dirty_full(DirtyRectTracker *tracker)
{
    tracker->full_redraw = true;
    tracker->count = 0;
}

void dirty_clear(DirtyRectTracker *tracker)
{
    tracker->count = 0;
    tracker->full_redraw = false;
}

bool dirty_needs_redraw(DirtyRectTracker *tracker)
{
    if (!tracker->enabled) {
        return true;
    }
    return tracker->full_redraw || tracker->count > 0;
}

bool dirty_intersects(DirtyRectTracker *tracker, int x, int y, int width, int height)
{
    if (!tracker->enabled || tracker->full_redraw) {
        return true;
    }

    for (int i = 0; i < tracker->count; i++) {
        DirtyRect *r = &tracker->rects[i];
        if (x < r->x + r->width && x + width > r->x &&
            y < r->y + r->height && y + height > r->y) {
            return true;
        }
    }
    return false;
}

void dirty_set_enabled(DirtyRectTracker *tracker, bool enabled)
{
    tracker->enabled = enabled;
    if (!enabled) {
        tracker->full_redraw = true;
    }
}

//=============================================================================
// Lazy Loading Queue Implementation
//=============================================================================

void lazy_init(LazyLoadQueue *queue)
{
    memset(queue, 0, sizeof(LazyLoadQueue));
    queue->enabled = true;
}

void lazy_free(LazyLoadQueue *queue)
{
    lazy_cancel_all(queue);
}

static int compare_priority(const void *a, const void *b)
{
    const LazyTask *ta = (const LazyTask *)a;
    const LazyTask *tb = (const LazyTask *)b;
    return tb->priority - ta->priority;  // Higher priority first
}

int lazy_enqueue(LazyLoadQueue *queue, LazyTaskType type, const char *path, int priority)
{
    if (!queue->enabled) {
        return -1;
    }

    // Check if already in queue
    for (int i = 0; i < queue->count; i++) {
        if (queue->tasks[i].type == type && strcmp(queue->tasks[i].path, path) == 0) {
            // Update priority if higher
            if (priority > queue->tasks[i].priority) {
                queue->tasks[i].priority = priority;
                qsort(queue->tasks, queue->count, sizeof(LazyTask), compare_priority);
            }
            return i;
        }
    }

    if (queue->count >= LAZY_QUEUE_MAX) {
        // Remove lowest priority task
        queue->count--;
        if (queue->tasks[queue->count].result) {
            free(queue->tasks[queue->count].result);
        }
    }

    LazyTask *task = &queue->tasks[queue->count];
    task->type = type;
    task->status = LAZY_STATUS_PENDING;
    strncpy(task->path, path, sizeof(task->path) - 1);
    task->path[sizeof(task->path) - 1] = '\0';
    task->priority = priority;
    task->result = NULL;
    task->start_time = 0;

    queue->count++;
    qsort(queue->tasks, queue->count, sizeof(LazyTask), compare_priority);

    return queue->count - 1;
}

void lazy_cancel(LazyLoadQueue *queue, const char *path)
{
    for (int i = 0; i < queue->count; i++) {
        if (strcmp(queue->tasks[i].path, path) == 0) {
            if (queue->tasks[i].result) {
                free(queue->tasks[i].result);
            }
            // Move remaining tasks
            memmove(&queue->tasks[i], &queue->tasks[i + 1],
                    (queue->count - i - 1) * sizeof(LazyTask));
            queue->count--;
            i--;  // Check same index again
        }
    }
}

void lazy_cancel_all(LazyLoadQueue *queue)
{
    for (int i = 0; i < queue->count; i++) {
        if (queue->tasks[i].result) {
            free(queue->tasks[i].result);
            queue->tasks[i].result = NULL;
        }
    }
    queue->count = 0;
    queue->processing = 0;
}

bool lazy_process_one(LazyLoadQueue *queue)
{
    if (!queue->enabled || queue->count == 0) {
        return false;
    }

    // Find first pending task
    for (int i = 0; i < queue->count; i++) {
        LazyTask *task = &queue->tasks[i];
        if (task->status == LAZY_STATUS_PENDING) {
            task->status = LAZY_STATUS_IN_PROGRESS;
            task->start_time = get_time_seconds();
            queue->processing++;

            // Process based on type
            switch (task->type) {
                case LAZY_TASK_THUMBNAIL:
                    // Placeholder: In real implementation, load and resize image
                    task->result = NULL;
                    task->status = LAZY_STATUS_COMPLETE;
                    break;

                case LAZY_TASK_FILE_INFO:
                    // Placeholder: Get extended file info
                    task->result = NULL;
                    task->status = LAZY_STATUS_COMPLETE;
                    break;

                case LAZY_TASK_PREVIEW:
                    // Placeholder: Generate preview
                    task->result = NULL;
                    task->status = LAZY_STATUS_COMPLETE;
                    break;
            }

            queue->processing--;
            return true;
        }
    }

    return false;
}

void* lazy_get_result(LazyLoadQueue *queue, const char *path, LazyTaskType type)
{
    for (int i = 0; i < queue->count; i++) {
        LazyTask *task = &queue->tasks[i];
        if (task->type == type && strcmp(task->path, path) == 0) {
            if (task->status == LAZY_STATUS_COMPLETE) {
                return task->result;
            }
            break;
        }
    }
    return NULL;
}

bool lazy_is_complete(LazyLoadQueue *queue, const char *path, LazyTaskType type)
{
    for (int i = 0; i < queue->count; i++) {
        LazyTask *task = &queue->tasks[i];
        if (task->type == type && strcmp(task->path, path) == 0) {
            return task->status == LAZY_STATUS_COMPLETE;
        }
    }
    return false;
}

//=============================================================================
// Memory Profiling Implementation
//=============================================================================

static bool profiling_active = false;

void memory_profile_start(void)
{
    profiling_active = true;
    memset(&g_memory_stats, 0, sizeof(g_memory_stats));
}

void memory_profile_stop(void)
{
    profiling_active = false;
}

void memory_profile_snapshot(MemoryStats *stats)
{
    memcpy(stats, &g_memory_stats, sizeof(MemoryStats));
}

void memory_profile_print(void)
{
    printf("=== Memory Profile ===\n");
    printf("Total allocated: %zu bytes (%.2f MB)\n",
           g_memory_stats.total_allocated,
           g_memory_stats.total_allocated / (1024.0 * 1024.0));
    printf("Peak allocated: %zu bytes (%.2f MB)\n",
           g_memory_stats.peak_allocated,
           g_memory_stats.peak_allocated / (1024.0 * 1024.0));
    printf("Allocations: %zu, Frees: %zu\n",
           g_memory_stats.allocation_count,
           g_memory_stats.free_count);
    printf("Directory cache: %zu bytes\n", g_memory_stats.directory_cache_bytes);
    printf("Thumbnail cache: %zu bytes\n", g_memory_stats.thumbnail_cache_bytes);
    printf("Preview: %zu bytes\n", g_memory_stats.preview_bytes);
}

void memory_track_alloc(size_t size)
{
    if (!profiling_active) return;

    g_memory_stats.total_allocated += size;
    g_memory_stats.allocation_count++;

    if (g_memory_stats.total_allocated > g_memory_stats.peak_allocated) {
        g_memory_stats.peak_allocated = g_memory_stats.total_allocated;
    }
}

void memory_track_free(size_t size)
{
    if (!profiling_active) return;

    if (size <= g_memory_stats.total_allocated) {
        g_memory_stats.total_allocated -= size;
    }
    g_memory_stats.free_count++;
}

//=============================================================================
// Frame Timing Implementation
//=============================================================================

void timing_init(FrameTimings *timings)
{
    memset(timings, 0, sizeof(FrameTimings));
    timings->min_frame_time = 1.0;  // Start with max
}

void timing_record_frame(FrameTimings *timings, double frame_time)
{
    timings->frame_times[timings->frame_index] = frame_time;
    timings->frame_index = (timings->frame_index + 1) % 120;
    timings->frame_count++;

    if (frame_time < timings->min_frame_time) {
        timings->min_frame_time = frame_time;
    }
    if (frame_time > timings->max_frame_time) {
        timings->max_frame_time = frame_time;
    }

    // Update average
    double sum = 0;
    int count = (timings->frame_count < 120) ? timings->frame_count : 120;
    for (int i = 0; i < count; i++) {
        sum += timings->frame_times[i];
    }
    timings->avg_frame_time = sum / count;
}

double timing_get_fps(FrameTimings *timings)
{
    if (timings->avg_frame_time <= 0) {
        return 0;
    }
    return 1.0 / timings->avg_frame_time;
}

static int compare_double_qsort(const void *a, const void *b)
{
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

double timing_get_percentile(FrameTimings *timings, float percentile)
{
    if (timings->frame_count == 0) {
        return 0;
    }

    int count = (timings->frame_count < 120) ? timings->frame_count : 120;

    // Copy and sort using qsort for O(n log n) performance
    double sorted[120];
    memcpy(sorted, timings->frame_times, count * sizeof(double));
    qsort(sorted, count, sizeof(double), compare_double_qsort);

    int idx = (int)(percentile * count);
    if (idx >= count) idx = count - 1;
    return sorted[idx];
}

bool timing_hitting_target(FrameTimings *timings, double target_fps)
{
    double target_time = 1.0 / target_fps;
    // Use 99th percentile to check for consistent performance
    double p99 = timing_get_percentile(timings, 0.99f);
    return p99 <= target_time * 1.1;  // Allow 10% margin
}

//=============================================================================
// Performance Manager Implementation
//=============================================================================

void perf_init(PerfManager *perf)
{
    dir_cache_init(&perf->dir_cache);
    dirty_init(&perf->dirty);
    lazy_init(&perf->lazy_queue);
    timing_init(&perf->timings);
    perf->profiling_enabled = false;
}

void perf_free(PerfManager *perf)
{
    dir_cache_free(&perf->dir_cache);
    lazy_free(&perf->lazy_queue);
}

void perf_update(PerfManager *perf, double frame_time)
{
    timing_record_frame(&perf->timings, frame_time);

    // Process some lazy load tasks
    for (int i = 0; i < 2; i++) {  // Process up to 2 per frame
        if (!lazy_process_one(&perf->lazy_queue)) {
            break;
        }
    }
}

void perf_set_enabled(PerfManager *perf, bool enabled)
{
    dir_cache_set_enabled(&perf->dir_cache, enabled);
    dirty_set_enabled(&perf->dirty, enabled);
    perf->lazy_queue.enabled = enabled;

    if (enabled) {
        memory_profile_start();
    } else {
        memory_profile_stop();
    }

    perf->profiling_enabled = enabled;
}

void perf_get_stats_string(PerfManager *perf, char *buffer, size_t buffer_size)
{
    double fps = timing_get_fps(&perf->timings);
    double p99 = timing_get_percentile(&perf->timings, 0.99f) * 1000;

    snprintf(buffer, buffer_size,
             "FPS: %.1f | P99: %.1fms | Cache: %d/%d | Lazy: %d",
             fps,
             p99,
             perf->dir_cache.count,
             DIR_CACHE_MAX_ENTRIES,
             perf->lazy_queue.count);
}

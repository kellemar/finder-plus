#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Test framework imports
extern void inc_tests_run(void);
extern void inc_tests_passed(void);
extern void inc_tests_failed(void);

#define TEST_ASSERT(condition, message) do { \
    inc_tests_run(); \
    if (condition) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s (line %d)\n", message, __LINE__); \
    } \
} while(0)

#define TEST_ASSERT_EQ(expected, actual, message) do { \
    inc_tests_run(); \
    if ((expected) == (actual)) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s - expected %d, got %d (line %d)\n", message, (int)(expected), (int)(actual), __LINE__); \
    } \
} while(0)

#include "../src/utils/perf.h"
#include "../src/core/filesystem.h"

//=============================================================================
// Directory Cache Tests
//=============================================================================

static void test_dir_cache_init(void)
{
    DirCache cache;
    dir_cache_init(&cache);

    TEST_ASSERT(cache.enabled, "Cache should be enabled by default");
    TEST_ASSERT_EQ(0, cache.count, "Cache should start empty");

    dir_cache_free(&cache);
}

static void test_dir_cache_put_get(void)
{
    DirCache cache;
    dir_cache_init(&cache);

    // Create a test directory state
    DirectoryState dir;
    directory_state_init(&dir);
    strncpy(dir.current_path, "/test/path", PATH_MAX_LEN - 1);
    dir.count = 0;

    // Put into cache
    dir_cache_put(&cache, "/test/path", &dir);
    TEST_ASSERT_EQ(1, cache.count, "Cache should have one entry");

    // Get from cache
    DirectoryState *cached = dir_cache_get(&cache, "/test/path");
    TEST_ASSERT(cached != NULL, "Should retrieve cached directory");
    TEST_ASSERT(strcmp(cached->current_path, "/test/path") == 0, "Cached path should match");

    // Get non-existent
    DirectoryState *missing = dir_cache_get(&cache, "/nonexistent");
    TEST_ASSERT(missing == NULL, "Should return NULL for non-cached path");

    directory_state_free(&dir);
    dir_cache_free(&cache);
}

static void test_dir_cache_invalidate(void)
{
    DirCache cache;
    dir_cache_init(&cache);

    DirectoryState dir1, dir2, dir3;
    directory_state_init(&dir1);
    directory_state_init(&dir2);
    directory_state_init(&dir3);

    strncpy(dir1.current_path, "/parent", PATH_MAX_LEN - 1);
    strncpy(dir2.current_path, "/parent/child", PATH_MAX_LEN - 1);
    strncpy(dir3.current_path, "/other", PATH_MAX_LEN - 1);

    dir_cache_put(&cache, "/parent", &dir1);
    dir_cache_put(&cache, "/parent/child", &dir2);
    dir_cache_put(&cache, "/other", &dir3);

    TEST_ASSERT_EQ(3, cache.count, "Should have 3 entries");

    // Invalidate parent (should also invalidate child)
    dir_cache_invalidate(&cache, "/parent");

    DirectoryState *cached1 = dir_cache_get(&cache, "/parent");
    DirectoryState *cached2 = dir_cache_get(&cache, "/parent/child");
    DirectoryState *cached3 = dir_cache_get(&cache, "/other");

    TEST_ASSERT(cached1 == NULL, "Parent should be invalidated");
    TEST_ASSERT(cached2 == NULL, "Child should be invalidated");
    TEST_ASSERT(cached3 != NULL, "Other should still be cached");

    directory_state_free(&dir1);
    directory_state_free(&dir2);
    directory_state_free(&dir3);
    dir_cache_free(&cache);
}

static void test_dir_cache_clear(void)
{
    DirCache cache;
    dir_cache_init(&cache);

    DirectoryState dir;
    directory_state_init(&dir);
    strncpy(dir.current_path, "/test", PATH_MAX_LEN - 1);

    dir_cache_put(&cache, "/test", &dir);
    dir_cache_put(&cache, "/test2", &dir);

    TEST_ASSERT_EQ(2, cache.count, "Should have 2 entries");

    dir_cache_clear(&cache);
    TEST_ASSERT_EQ(0, cache.count, "Cache should be empty after clear");

    directory_state_free(&dir);
    dir_cache_free(&cache);
}

static void test_dir_cache_disabled(void)
{
    DirCache cache;
    dir_cache_init(&cache);

    dir_cache_set_enabled(&cache, false);
    TEST_ASSERT(!cache.enabled, "Cache should be disabled");

    DirectoryState dir;
    directory_state_init(&dir);
    strncpy(dir.current_path, "/test", PATH_MAX_LEN - 1);

    dir_cache_put(&cache, "/test", &dir);
    TEST_ASSERT_EQ(0, cache.count, "Should not cache when disabled");

    DirectoryState *cached = dir_cache_get(&cache, "/test");
    TEST_ASSERT(cached == NULL, "Should return NULL when disabled");

    directory_state_free(&dir);
    dir_cache_free(&cache);
}

//=============================================================================
// Dirty Rectangle Tests
//=============================================================================

static void test_dirty_init(void)
{
    DirtyRectTracker tracker;
    dirty_init(&tracker);

    TEST_ASSERT(tracker.enabled, "Tracker should be enabled by default");
    TEST_ASSERT(tracker.full_redraw, "Should start with full redraw");
    TEST_ASSERT_EQ(0, tracker.count, "Should have no dirty rects");

    dirty_clear(&tracker);
    TEST_ASSERT(!tracker.full_redraw, "Full redraw should be cleared");
}

static void test_dirty_add(void)
{
    DirtyRectTracker tracker;
    dirty_init(&tracker);
    dirty_clear(&tracker);

    dirty_add(&tracker, 10, 10, 100, 50);
    TEST_ASSERT_EQ(1, tracker.count, "Should have one dirty rect");
    TEST_ASSERT(dirty_needs_redraw(&tracker), "Should need redraw");

    TEST_ASSERT(dirty_intersects(&tracker, 50, 30, 20, 20), "Should intersect with dirty area");
    TEST_ASSERT(!dirty_intersects(&tracker, 200, 200, 20, 20), "Should not intersect outside dirty area");
}

static void test_dirty_merge(void)
{
    DirtyRectTracker tracker;
    dirty_init(&tracker);
    dirty_clear(&tracker);

    // Add overlapping rects - should merge
    dirty_add(&tracker, 10, 10, 50, 50);
    dirty_add(&tracker, 40, 40, 50, 50);

    // Should merge into one rect (or at most 2 depending on margin)
    TEST_ASSERT(tracker.count <= 2, "Overlapping rects should merge");
}

static void test_dirty_full(void)
{
    DirtyRectTracker tracker;
    dirty_init(&tracker);
    dirty_clear(&tracker);

    dirty_add(&tracker, 10, 10, 50, 50);
    dirty_full(&tracker);

    TEST_ASSERT(tracker.full_redraw, "Should be set to full redraw");
    TEST_ASSERT(dirty_needs_redraw(&tracker), "Should need redraw");
    TEST_ASSERT(dirty_intersects(&tracker, 500, 500, 10, 10), "Any rect should intersect in full redraw");
}

static void test_dirty_disabled(void)
{
    DirtyRectTracker tracker;
    dirty_init(&tracker);
    dirty_clear(&tracker);
    dirty_set_enabled(&tracker, false);

    TEST_ASSERT(!tracker.enabled, "Should be disabled");
    TEST_ASSERT(dirty_needs_redraw(&tracker), "Should always need redraw when disabled");
}

//=============================================================================
// Lazy Loading Tests
//=============================================================================

static void test_lazy_init(void)
{
    LazyLoadQueue queue;
    lazy_init(&queue);

    TEST_ASSERT(queue.enabled, "Queue should be enabled by default");
    TEST_ASSERT_EQ(0, queue.count, "Queue should start empty");

    lazy_free(&queue);
}

static void test_lazy_enqueue(void)
{
    LazyLoadQueue queue;
    lazy_init(&queue);

    int idx = lazy_enqueue(&queue, LAZY_TASK_THUMBNAIL, "/test/image.png", 5);
    TEST_ASSERT(idx >= 0, "Should return valid index");
    TEST_ASSERT_EQ(1, queue.count, "Queue should have one task");

    // Enqueue same path - should not duplicate
    int idx2 = lazy_enqueue(&queue, LAZY_TASK_THUMBNAIL, "/test/image.png", 5);
    TEST_ASSERT_EQ(1, queue.count, "Should not duplicate task");
    (void)idx2;  // Suppress unused warning

    // Enqueue different path
    lazy_enqueue(&queue, LAZY_TASK_THUMBNAIL, "/test/other.png", 3);
    TEST_ASSERT_EQ(2, queue.count, "Should have two tasks");

    lazy_free(&queue);
}

static void test_lazy_cancel(void)
{
    LazyLoadQueue queue;
    lazy_init(&queue);

    lazy_enqueue(&queue, LAZY_TASK_THUMBNAIL, "/test/a.png", 5);
    lazy_enqueue(&queue, LAZY_TASK_THUMBNAIL, "/test/b.png", 5);
    TEST_ASSERT_EQ(2, queue.count, "Should have two tasks");

    lazy_cancel(&queue, "/test/a.png");
    TEST_ASSERT_EQ(1, queue.count, "Should have one task after cancel");

    lazy_cancel_all(&queue);
    TEST_ASSERT_EQ(0, queue.count, "Should be empty after cancel all");

    lazy_free(&queue);
}

static void test_lazy_process(void)
{
    LazyLoadQueue queue;
    lazy_init(&queue);

    lazy_enqueue(&queue, LAZY_TASK_THUMBNAIL, "/test/image.png", 5);

    bool processed = lazy_process_one(&queue);
    TEST_ASSERT(processed, "Should process one task");
    TEST_ASSERT(lazy_is_complete(&queue, "/test/image.png", LAZY_TASK_THUMBNAIL),
                "Task should be complete after processing");

    lazy_free(&queue);
}

static void test_lazy_priority(void)
{
    LazyLoadQueue queue;
    lazy_init(&queue);

    lazy_enqueue(&queue, LAZY_TASK_THUMBNAIL, "/test/low.png", 1);
    lazy_enqueue(&queue, LAZY_TASK_THUMBNAIL, "/test/high.png", 10);
    lazy_enqueue(&queue, LAZY_TASK_THUMBNAIL, "/test/medium.png", 5);

    // First task should be high priority
    TEST_ASSERT(queue.tasks[0].priority == 10, "Highest priority should be first");

    lazy_free(&queue);
}

//=============================================================================
// Memory Profiling Tests
//=============================================================================

static void test_memory_profiling(void)
{
    memory_profile_start();

    memory_track_alloc(1024);
    TEST_ASSERT(g_memory_stats.total_allocated == 1024, "Should track allocation");
    TEST_ASSERT(g_memory_stats.allocation_count == 1, "Should count allocation");

    memory_track_alloc(512);
    TEST_ASSERT(g_memory_stats.total_allocated == 1536, "Should accumulate allocations");
    TEST_ASSERT(g_memory_stats.peak_allocated == 1536, "Peak should update");

    memory_track_free(1024);
    TEST_ASSERT(g_memory_stats.total_allocated == 512, "Should track free");
    TEST_ASSERT(g_memory_stats.free_count == 1, "Should count free");
    TEST_ASSERT(g_memory_stats.peak_allocated == 1536, "Peak should not decrease");

    memory_profile_stop();
}

static void test_memory_snapshot(void)
{
    memory_profile_start();
    memory_track_alloc(2048);

    MemoryStats snapshot;
    memory_profile_snapshot(&snapshot);

    TEST_ASSERT(snapshot.total_allocated == 2048, "Snapshot should capture current state");

    memory_track_free(2048);
    memory_profile_stop();
}

//=============================================================================
// Frame Timing Tests
//=============================================================================

static void test_timing_init(void)
{
    FrameTimings timings;
    timing_init(&timings);

    TEST_ASSERT_EQ(0, timings.frame_count, "Should start with zero frames");
    TEST_ASSERT(timings.min_frame_time > 0, "Min frame time should be initialized");
}

static void test_timing_record(void)
{
    FrameTimings timings;
    timing_init(&timings);

    // Record some frames at 60 FPS (16.67ms)
    for (int i = 0; i < 60; i++) {
        timing_record_frame(&timings, 0.01667);
    }

    double fps = timing_get_fps(&timings);
    TEST_ASSERT(fps > 58 && fps < 62, "FPS should be close to 60");

    bool hitting = timing_hitting_target(&timings, 60.0);
    TEST_ASSERT(hitting, "Should be hitting 60 FPS target");
}

static void test_timing_percentile(void)
{
    FrameTimings timings;
    timing_init(&timings);

    // Record mixed frame times
    for (int i = 0; i < 100; i++) {
        double time = (i % 10 == 0) ? 0.033 : 0.016;  // 10% at 30fps, 90% at 60fps
        timing_record_frame(&timings, time);
    }

    double p50 = timing_get_percentile(&timings, 0.5f);
    double p99 = timing_get_percentile(&timings, 0.99f);

    TEST_ASSERT(p50 < p99, "P99 should be higher than P50");
}

//=============================================================================
// Performance Manager Tests
//=============================================================================

static void test_perf_manager_init(void)
{
    PerfManager perf;
    perf_init(&perf);

    TEST_ASSERT(perf.dir_cache.enabled, "Dir cache should be enabled");
    TEST_ASSERT(perf.dirty.enabled, "Dirty tracker should be enabled");
    TEST_ASSERT(perf.lazy_queue.enabled, "Lazy queue should be enabled");

    perf_free(&perf);
}

static void test_perf_manager_update(void)
{
    PerfManager perf;
    perf_init(&perf);

    // Add some lazy tasks
    lazy_enqueue(&perf.lazy_queue, LAZY_TASK_THUMBNAIL, "/test.png", 5);

    perf_update(&perf, 0.016);

    TEST_ASSERT(perf.timings.frame_count == 1, "Should record frame");
    // Task should be processed during update
    TEST_ASSERT(lazy_is_complete(&perf.lazy_queue, "/test.png", LAZY_TASK_THUMBNAIL),
                "Lazy task should be processed");

    perf_free(&perf);
}

static void test_perf_stats_string(void)
{
    PerfManager perf;
    perf_init(&perf);

    for (int i = 0; i < 10; i++) {
        perf_update(&perf, 0.016);
    }

    char buffer[256];
    perf_get_stats_string(&perf, buffer, sizeof(buffer));

    TEST_ASSERT(strlen(buffer) > 0, "Stats string should not be empty");
    TEST_ASSERT(strstr(buffer, "FPS") != NULL, "Stats should include FPS");

    perf_free(&perf);
}

static void test_perf_enable_disable(void)
{
    PerfManager perf;
    perf_init(&perf);

    perf_set_enabled(&perf, false);
    TEST_ASSERT(!perf.dir_cache.enabled, "Dir cache should be disabled");
    TEST_ASSERT(!perf.dirty.enabled, "Dirty tracker should be disabled");
    TEST_ASSERT(!perf.lazy_queue.enabled, "Lazy queue should be disabled");

    perf_set_enabled(&perf, true);
    TEST_ASSERT(perf.dir_cache.enabled, "Dir cache should be re-enabled");

    perf_free(&perf);
}

//=============================================================================
// Main Test Function
//=============================================================================

void test_perf(void)
{
    printf("  [Directory Cache]\n");
    test_dir_cache_init();
    test_dir_cache_put_get();
    test_dir_cache_invalidate();
    test_dir_cache_clear();
    test_dir_cache_disabled();

    printf("  [Dirty Rectangles]\n");
    test_dirty_init();
    test_dirty_add();
    test_dirty_merge();
    test_dirty_full();
    test_dirty_disabled();

    printf("  [Lazy Loading]\n");
    test_lazy_init();
    test_lazy_enqueue();
    test_lazy_cancel();
    test_lazy_process();
    test_lazy_priority();

    printf("  [Memory Profiling]\n");
    test_memory_profiling();
    test_memory_snapshot();

    printf("  [Frame Timing]\n");
    test_timing_init();
    test_timing_record();
    test_timing_percentile();

    printf("  [Performance Manager]\n");
    test_perf_manager_init();
    test_perf_manager_update();
    test_perf_stats_string();
    test_perf_enable_disable();
}

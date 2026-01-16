#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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

#include "../src/ui/dual_pane.h"
#include "../src/core/filesystem.h"

// Test directory path
static char test_dir[PATH_MAX_LEN];

static void setup_test_dir(void)
{
    snprintf(test_dir, sizeof(test_dir), "/tmp/finder_plus_dual_pane_test_%d", getpid());
    mkdir(test_dir, 0755);

    // Create some test files and directories
    char path[PATH_MAX_LEN];

    snprintf(path, sizeof(path), "%s/file1.txt", test_dir);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "test content 1"); fclose(f); }

    snprintf(path, sizeof(path), "%s/file2.txt", test_dir);
    f = fopen(path, "w");
    if (f) { fprintf(f, "test content 2"); fclose(f); }

    snprintf(path, sizeof(path), "%s/subdir1", test_dir);
    mkdir(path, 0755);

    snprintf(path, sizeof(path), "%s/subdir2", test_dir);
    mkdir(path, 0755);

    // Create files in subdir1
    snprintf(path, sizeof(path), "%s/subdir1/nested.txt", test_dir);
    f = fopen(path, "w");
    if (f) { fprintf(f, "nested content"); fclose(f); }
}

static void cleanup_test_dir(void)
{
    char cmd[PATH_MAX_LEN + 32];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", test_dir);
    int result = system(cmd);
    (void)result;
}

static void test_dual_pane_init(void)
{
    DualPaneState state;
    dual_pane_init(&state);

    TEST_ASSERT(!state.enabled, "Dual pane should be disabled by default");
    TEST_ASSERT_EQ(PANE_LEFT, state.active_pane, "Active pane should be left by default");
    TEST_ASSERT(!state.sync_scroll, "Sync scroll should be disabled by default");
    TEST_ASSERT(!state.compare_mode, "Compare mode should be disabled by default");

    dual_pane_free(&state);
}

static void test_dual_pane_toggle(void)
{
    DualPaneState state;
    dual_pane_init(&state);

    TEST_ASSERT(!dual_pane_is_enabled(&state), "Should start disabled");

    // Can't fully test toggle without App struct, but can test state
    state.enabled = true;
    TEST_ASSERT(dual_pane_is_enabled(&state), "Should be enabled after setting");

    state.enabled = false;
    TEST_ASSERT(!dual_pane_is_enabled(&state), "Should be disabled after clearing");

    dual_pane_free(&state);
}

static void test_pane_state(void)
{
    DualPaneState state;
    dual_pane_init(&state);

    // Test active pane getters
    state.active_pane = PANE_LEFT;
    PaneState *active = dual_pane_get_active_pane(&state);
    PaneState *inactive = dual_pane_get_inactive_pane(&state);

    TEST_ASSERT(active == &state.left, "Active pane should be left");
    TEST_ASSERT(inactive == &state.right, "Inactive pane should be right");

    state.active_pane = PANE_RIGHT;
    active = dual_pane_get_active_pane(&state);
    inactive = dual_pane_get_inactive_pane(&state);

    TEST_ASSERT(active == &state.right, "Active pane should be right");
    TEST_ASSERT(inactive == &state.left, "Inactive pane should be left");

    dual_pane_free(&state);
}

static void test_pane_directory_loading(void)
{
    DualPaneState state;
    dual_pane_init(&state);

    // Load directory into left pane
    strncpy(state.left.current_path, test_dir, PATH_MAX_LEN - 1);
    bool result = directory_read(&state.left.directory, test_dir);

    TEST_ASSERT(result, "Should load test directory into left pane");
    TEST_ASSERT(state.left.directory.count > 0, "Left pane should have entries");

    // Load same directory into right pane
    strncpy(state.right.current_path, test_dir, PATH_MAX_LEN - 1);
    result = directory_read(&state.right.directory, test_dir);

    TEST_ASSERT(result, "Should load test directory into right pane");
    TEST_ASSERT_EQ(state.left.directory.count, state.right.directory.count, "Both panes should have same count");

    dual_pane_free(&state);
}

static void test_sync_scroll(void)
{
    DualPaneState state;
    dual_pane_init(&state);

    TEST_ASSERT(!state.sync_scroll, "Sync scroll should start disabled");

    dual_pane_toggle_sync_scroll(&state);
    TEST_ASSERT(state.sync_scroll, "Sync scroll should be enabled after toggle");

    dual_pane_toggle_sync_scroll(&state);
    TEST_ASSERT(!state.sync_scroll, "Sync scroll should be disabled after second toggle");

    dual_pane_free(&state);
}

static void test_compare_result_values(void)
{
    // Test that compare result enum values are distinct
    TEST_ASSERT(COMPARE_SAME != COMPARE_DIFFERENT, "COMPARE_SAME should differ from COMPARE_DIFFERENT");
    TEST_ASSERT(COMPARE_LEFT_ONLY != COMPARE_RIGHT_ONLY, "COMPARE_LEFT_ONLY should differ from COMPARE_RIGHT_ONLY");
    TEST_ASSERT(COMPARE_DIR != COMPARE_SAME, "COMPARE_DIR should differ from COMPARE_SAME");
}

static void test_dual_pane_free(void)
{
    DualPaneState state;
    dual_pane_init(&state);

    // Load some data
    strncpy(state.left.current_path, test_dir, PATH_MAX_LEN - 1);
    directory_read(&state.left.directory, test_dir);

    // Simulate comparison results
    state.compare_results_left = malloc(10 * sizeof(CompareResult));
    state.compare_count_left = 10;

    dual_pane_free(&state);

    // After free, pointers should be NULL
    TEST_ASSERT(state.compare_results_left == NULL, "Compare results should be NULL after free");
    TEST_ASSERT(state.compare_results_right == NULL, "Compare results right should be NULL after free");

    // Verify we can init and free again without issues
    dual_pane_init(&state);
    dual_pane_free(&state);
    TEST_ASSERT(true, "Re-init and free should work without crash");
}

void test_dual_pane(void)
{
    setup_test_dir();

    test_dual_pane_init();
    test_dual_pane_toggle();
    test_pane_state();
    test_pane_directory_loading();
    test_sync_scroll();
    test_compare_result_values();
    test_dual_pane_free();

    cleanup_test_dir();
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Test framework imports from test_main.c
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

// Include headers for the modules we're testing
#include "../src/core/operations.h"
#include "../src/platform/clipboard.h"

// Test platform clipboard text operations
static void test_clipboard_text(void)
{
    printf("  [Clipboard Text Operations]\n");

    // Test copying text
    bool result = platform_clipboard_copy_text("Hello from Finder Plus test");
    TEST_ASSERT(result, "Copy text to clipboard");

    // Test retrieving text
    const char *text = platform_clipboard_get_text();
    TEST_ASSERT(text != NULL, "Get text from clipboard returns non-null");

    if (text != NULL) {
        TEST_ASSERT(strcmp(text, "Hello from Finder Plus test") == 0, "Clipboard text matches");
    }
}

// Test platform clipboard file operations
static void test_clipboard_files(void)
{
    printf("  [Clipboard File Operations]\n");

    // Create test file paths
    const char *paths[] = {
        "/tmp",
        "/usr"
    };

    // Test copying files
    bool result = platform_clipboard_copy_files(paths, 2);
    TEST_ASSERT(result, "Copy file paths to clipboard");

    // Test checking for files
    bool has_files = platform_clipboard_has_files();
    TEST_ASSERT(has_files, "Clipboard has files after copy");

    // Test getting file count
    int count = platform_clipboard_get_file_count();
    TEST_ASSERT_EQ(2, count, "Clipboard file count matches");

    // Test getting file paths
    const char *path1 = platform_clipboard_get_file_path(0);
    TEST_ASSERT(path1 != NULL, "Get first file path");
    if (path1) {
        TEST_ASSERT(strcmp(path1, "/tmp") == 0, "First path matches");
    }

    const char *path2 = platform_clipboard_get_file_path(1);
    TEST_ASSERT(path2 != NULL, "Get second file path");
    if (path2) {
        TEST_ASSERT(strcmp(path2, "/usr") == 0, "Second path matches");
    }

    // Test out of range
    const char *path_invalid = platform_clipboard_get_file_path(10);
    TEST_ASSERT(path_invalid == NULL, "Out of range returns NULL");
}

// Test clipboard sync to system
static void test_clipboard_sync(void)
{
    printf("  [Clipboard Sync Operations]\n");

    ClipboardState clipboard;
    clipboard_init(&clipboard);

    // Clear clipboard first
    platform_clipboard_clear();

    // Copy some files to system clipboard
    const char *paths[] = {
        "/tmp",
        "/var"
    };
    platform_clipboard_copy_files(paths, 2);

    // Sync from system
    clipboard_sync_from_system(&clipboard);

    // Verify internal state was updated
    TEST_ASSERT_EQ(2, clipboard.count, "Internal clipboard count after sync");
    TEST_ASSERT(clipboard.operation == OP_COPY, "Synced clipboard operation is COPY");

    if (clipboard.count >= 2) {
        TEST_ASSERT(strcmp(clipboard.paths[0], "/tmp") == 0, "First synced path matches");
        TEST_ASSERT(strcmp(clipboard.paths[1], "/var") == 0, "Second synced path matches");
    }
}

// Test clipboard copy paths as text
static void test_clipboard_paths_as_text(void)
{
    printf("  [Clipboard Paths as Text]\n");

    ClipboardState clipboard;
    clipboard_init(&clipboard);

    const char *paths[] = {
        "/path/to/file1.txt",
        "/path/to/file2.txt"
    };

    clipboard_copy(&clipboard, paths, 2);

    // Now copy as text
    bool result = clipboard_copy_paths_as_text(&clipboard);
    TEST_ASSERT(result, "Copy paths as text succeeds");

    // Get the text and verify
    const char *text = platform_clipboard_get_text();
    TEST_ASSERT(text != NULL, "Text from paths is non-null");

    if (text != NULL) {
        // Should contain both paths
        TEST_ASSERT(strstr(text, "/path/to/file1.txt") != NULL, "Text contains first path");
        TEST_ASSERT(strstr(text, "/path/to/file2.txt") != NULL, "Text contains second path");
    }
}

// Main test function
void test_phase2(void)
{
    test_clipboard_text();
    test_clipboard_files();
    test_clipboard_sync();
    test_clipboard_paths_as_text();
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "core/operations.h"
#include "core/filesystem.h"

// Test helper functions
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

#define TEST_ASSERT_STR_EQ(expected, actual, message) do { \
    inc_tests_run(); \
    if (strcmp((expected), (actual)) == 0) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s - expected '%s', got '%s' (line %d)\n", message, (expected), (actual), __LINE__); \
    } \
} while(0)

static char test_dir[512] = {0};
static const char *test_dir_base = "/tmp/finder_plus_ops_test";

static void setup_test_dir(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_dir_base);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "mkdir -p %s/subdir", test_dir_base);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "echo 'test content' > %s/file1.txt", test_dir_base);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "echo 'more content' > %s/file2.txt", test_dir_base);
    system(cmd);

    if (realpath(test_dir_base, test_dir) == NULL) {
        strncpy(test_dir, test_dir_base, sizeof(test_dir) - 1);
    }
}

static void teardown_test_dir(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_dir_base);
    system(cmd);
}

void test_operations(void)
{
    printf("  Setting up test environment...\n");
    setup_test_dir();

    // Test clipboard initialization
    {
        ClipboardState clipboard;
        clipboard_init(&clipboard);

        TEST_ASSERT(clipboard.count == 0, "Clipboard should start empty");
        TEST_ASSERT(clipboard.operation == OP_NONE, "Operation should be OP_NONE");
        TEST_ASSERT(!clipboard_has_items(&clipboard), "has_items should be false");
    }

    // Test clipboard copy
    {
        ClipboardState clipboard;
        clipboard_init(&clipboard);

        char path1[512];
        snprintf(path1, sizeof(path1), "%s/file1.txt", test_dir);
        const char *paths[] = { path1 };

        clipboard_copy(&clipboard, paths, 1);

        TEST_ASSERT(clipboard.count == 1, "Clipboard should have 1 item");
        TEST_ASSERT(clipboard.operation == OP_COPY, "Operation should be OP_COPY");
        TEST_ASSERT(clipboard_has_items(&clipboard), "has_items should be true");
        TEST_ASSERT(clipboard_contains(&clipboard, path1), "Should contain copied path");
    }

    // Test clipboard cut
    {
        ClipboardState clipboard;
        clipboard_init(&clipboard);

        char path1[512];
        snprintf(path1, sizeof(path1), "%s/file1.txt", test_dir);
        const char *paths[] = { path1 };

        clipboard_cut(&clipboard, paths, 1);

        TEST_ASSERT(clipboard.count == 1, "Clipboard should have 1 item");
        TEST_ASSERT(clipboard.operation == OP_CUT, "Operation should be OP_CUT");
    }

    // Test clipboard clear
    {
        ClipboardState clipboard;
        clipboard_init(&clipboard);

        char path1[512];
        snprintf(path1, sizeof(path1), "%s/file1.txt", test_dir);
        const char *paths[] = { path1 };

        clipboard_copy(&clipboard, paths, 1);
        clipboard_clear(&clipboard);

        TEST_ASSERT(clipboard.count == 0, "Clipboard should be empty after clear");
        TEST_ASSERT(!clipboard_has_items(&clipboard), "has_items should be false after clear");
    }

    // Test file_copy
    {
        char source[512];
        snprintf(source, sizeof(source), "%s/file1.txt", test_dir);

        char dest_dir[512];
        snprintf(dest_dir, sizeof(dest_dir), "%s/subdir", test_dir);

        OperationResult result = file_copy(source, dest_dir);
        TEST_ASSERT(result == OP_SUCCESS, "file_copy should succeed");

        char copied_file[512];
        snprintf(copied_file, sizeof(copied_file), "%s/subdir/file1.txt", test_dir);

        struct stat st;
        TEST_ASSERT(stat(copied_file, &st) == 0, "Copied file should exist");
        TEST_ASSERT(stat(source, &st) == 0, "Original file should still exist");
    }

    // Test file_move
    {
        char source[512];
        snprintf(source, sizeof(source), "%s/file2.txt", test_dir);

        char dest_dir[512];
        snprintf(dest_dir, sizeof(dest_dir), "%s/subdir", test_dir);

        struct stat st;
        TEST_ASSERT(stat(source, &st) == 0, "Source file should exist before move");

        OperationResult result = file_move(source, dest_dir);
        TEST_ASSERT(result == OP_SUCCESS, "file_move should succeed");

        char moved_file[512];
        snprintf(moved_file, sizeof(moved_file), "%s/subdir/file2.txt", test_dir);

        TEST_ASSERT(stat(moved_file, &st) == 0, "Moved file should exist");
        TEST_ASSERT(stat(source, &st) != 0, "Original file should not exist after move");
    }

    // Test file_rename
    {
        char source[512];
        snprintf(source, sizeof(source), "%s/subdir/file1.txt", test_dir);

        OperationResult result = file_rename(source, "renamed.txt");
        TEST_ASSERT(result == OP_SUCCESS, "file_rename should succeed");

        char renamed_file[512];
        snprintf(renamed_file, sizeof(renamed_file), "%s/subdir/renamed.txt", test_dir);

        struct stat st;
        TEST_ASSERT(stat(renamed_file, &st) == 0, "Renamed file should exist");
        TEST_ASSERT(stat(source, &st) != 0, "Original name should not exist");
    }

    // Test file_create_directory
    {
        OperationResult result = file_create_directory(test_dir, "new_folder");
        TEST_ASSERT(result == OP_SUCCESS, "file_create_directory should succeed");

        char new_dir[512];
        snprintf(new_dir, sizeof(new_dir), "%s/new_folder", test_dir);

        struct stat st;
        TEST_ASSERT(stat(new_dir, &st) == 0, "New directory should exist");
        TEST_ASSERT(S_ISDIR(st.st_mode), "Should be a directory");
    }

    // Test file_create_file (empty file)
    {
        OperationResult result = file_create_file(test_dir, "new_file.txt", NULL);
        TEST_ASSERT(result == OP_SUCCESS, "file_create_file should succeed");

        char new_file[512];
        snprintf(new_file, sizeof(new_file), "%s/new_file.txt", test_dir);

        struct stat st;
        TEST_ASSERT(stat(new_file, &st) == 0, "New file should exist");
        TEST_ASSERT(S_ISREG(st.st_mode), "Should be a regular file");
        TEST_ASSERT(st.st_size == 0, "Empty file should have size 0");
    }

    // Test file_create_file with content
    {
        const char *content = "Hello, World!\nThis is a test file.";
        OperationResult result = file_create_file(test_dir, "file_with_content.txt", content);
        TEST_ASSERT(result == OP_SUCCESS, "file_create_file with content should succeed");

        char new_file[512];
        snprintf(new_file, sizeof(new_file), "%s/file_with_content.txt", test_dir);

        struct stat st;
        TEST_ASSERT(stat(new_file, &st) == 0, "File with content should exist");
        TEST_ASSERT(S_ISREG(st.st_mode), "Should be a regular file");
        TEST_ASSERT(st.st_size == (off_t)strlen(content), "File size should match content length");

        // Verify content was written correctly
        FILE *f = fopen(new_file, "r");
        TEST_ASSERT(f != NULL, "Should be able to open file for reading");
        char read_buffer[256];
        size_t bytes_read = fread(read_buffer, 1, sizeof(read_buffer) - 1, f);
        read_buffer[bytes_read] = '\0';
        fclose(f);
        TEST_ASSERT_STR_EQ(read_buffer, content, "File content should match");
    }

    // Test file_duplicate
    {
        char source[512];
        snprintf(source, sizeof(source), "%s/new_file.txt", test_dir);

        OperationResult result = file_duplicate(source);
        TEST_ASSERT(result == OP_SUCCESS, "file_duplicate should succeed");

        // Check for duplicated file (should have " (1)" suffix)
        char dup_file[512];
        snprintf(dup_file, sizeof(dup_file), "%s/new_file (1).txt", test_dir);

        struct stat st;
        TEST_ASSERT(stat(dup_file, &st) == 0, "Duplicated file should exist");
    }

    // Test unique name generation
    {
        char base_path[512];
        snprintf(base_path, sizeof(base_path), "%s/new_file.txt", test_dir);

        char unique_path[512];
        generate_unique_name(base_path, unique_path, sizeof(unique_path));

        // Since new_file.txt and new_file (1).txt exist, should get new_file (2).txt
        char expected[512];
        snprintf(expected, sizeof(expected), "%s/new_file (2).txt", test_dir);

        TEST_ASSERT(strcmp(unique_path, expected) == 0, "Should generate unique name (2)");
    }

    // Test error handling - copy nonexistent file
    {
        OperationResult result = file_copy("/nonexistent/path/file.txt", test_dir);
        TEST_ASSERT(result == OP_ERROR_NOT_FOUND, "Should return NOT_FOUND for nonexistent source");
    }

    // Test error handling - invalid rename
    {
        char source[512];
        snprintf(source, sizeof(source), "%s/new_file.txt", test_dir);

        OperationResult result = file_rename(source, "invalid/name");
        TEST_ASSERT(result == OP_ERROR_INVALID, "Should return INVALID for name with slash");
    }

    printf("  Cleaning up test environment...\n");
    teardown_test_dir();
}

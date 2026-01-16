#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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

// Test directory path (created during tests)
// On macOS, /tmp is a symlink to /private/tmp, so we use the resolved path
static char test_dir[512] = {0};
static const char *test_dir_base = "/tmp/finder_plus_test";

// Setup function - create test directory structure
static void setup_test_dir(void)
{
    char cmd[512];

    // Remove if exists
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_dir_base);
    system(cmd);

    // Create test directory structure
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/subdir1 %s/subdir2 %s/.hidden_dir", test_dir_base, test_dir_base, test_dir_base);
    system(cmd);

    // Create test files
    snprintf(cmd, sizeof(cmd), "touch %s/file1.txt %s/file2.c %s/file3.md", test_dir_base, test_dir_base, test_dir_base);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "touch %s/.hidden_file", test_dir_base);
    system(cmd);

    // Create file with content (for size testing)
    snprintf(cmd, sizeof(cmd), "echo 'Hello, World!' > %s/hello.txt", test_dir_base);
    system(cmd);

    // Create symlink
    snprintf(cmd, sizeof(cmd), "ln -s %s/file1.txt %s/link_to_file1.txt", test_dir_base, test_dir_base);
    system(cmd);

    // Resolve the actual path (on macOS, /tmp is a symlink to /private/tmp)
    if (realpath(test_dir_base, test_dir) == NULL) {
        strncpy(test_dir, test_dir_base, sizeof(test_dir) - 1);
        test_dir[sizeof(test_dir) - 1] = '\0';
    }
}

// Teardown function
static void teardown_test_dir(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_dir_base);
    system(cmd);
}

void test_filesystem(void)
{
    printf("  Setting up test environment...\n");
    setup_test_dir();

    // Test: directory_state_init
    {
        DirectoryState state;
        directory_state_init(&state);

        TEST_ASSERT(state.entries == NULL, "Initial entries should be NULL");
        TEST_ASSERT_EQ(0, state.count, "Initial count should be 0");
        TEST_ASSERT_EQ(0, state.capacity, "Initial capacity should be 0");
        TEST_ASSERT(state.show_hidden == false, "show_hidden should be false initially");
    }

    // Test: directory_read with valid path
    {
        DirectoryState state;
        directory_state_init(&state);

        bool result = directory_read(&state, test_dir);
        TEST_ASSERT(result, "Should successfully read test directory");
        TEST_ASSERT(state.count > 0, "Should have entries");
        TEST_ASSERT(state.error_message[0] == '\0', "Should have no error message");

        // Check that hidden files are not shown by default
        bool has_hidden = false;
        for (int i = 0; i < state.count; i++) {
            if (state.entries[i].is_hidden) {
                has_hidden = true;
                break;
            }
        }
        TEST_ASSERT(!has_hidden, "Should not show hidden files by default");

        directory_state_free(&state);
    }

    // Test: directory_read with hidden files enabled
    {
        DirectoryState state;
        directory_state_init(&state);
        state.show_hidden = true;

        bool result = directory_read(&state, test_dir);
        TEST_ASSERT(result, "Should successfully read with hidden files");

        bool has_hidden = false;
        for (int i = 0; i < state.count; i++) {
            if (state.entries[i].is_hidden) {
                has_hidden = true;
                break;
            }
        }
        TEST_ASSERT(has_hidden, "Should show hidden files when enabled");

        directory_state_free(&state);
    }

    // Test: directory_read with invalid path
    {
        DirectoryState state;
        directory_state_init(&state);

        bool result = directory_read(&state, "/nonexistent/path/12345");
        TEST_ASSERT(!result, "Should fail for nonexistent path");
        TEST_ASSERT(state.error_message[0] != '\0', "Should have error message");

        directory_state_free(&state);
    }

    // Test: directories come before files (sorting)
    {
        DirectoryState state;
        directory_state_init(&state);

        directory_read(&state, test_dir);

        bool dirs_first = true;
        bool seen_file = false;
        for (int i = 0; i < state.count; i++) {
            if (state.entries[i].is_directory) {
                if (seen_file) {
                    dirs_first = false;
                    break;
                }
            } else {
                seen_file = true;
            }
        }
        TEST_ASSERT(dirs_first, "Directories should come before files");

        directory_state_free(&state);
    }

    // Test: symlink detection
    {
        DirectoryState state;
        directory_state_init(&state);

        directory_read(&state, test_dir);

        bool found_symlink = false;
        for (int i = 0; i < state.count; i++) {
            if (strcmp(state.entries[i].name, "link_to_file1.txt") == 0) {
                TEST_ASSERT(state.entries[i].is_symlink, "Should detect symlink");
                found_symlink = true;
                break;
            }
        }
        TEST_ASSERT(found_symlink, "Should find the symlink file");

        directory_state_free(&state);
    }

    // Test: extension extraction
    {
        DirectoryState state;
        directory_state_init(&state);

        directory_read(&state, test_dir);

        bool found_c_file = false;
        for (int i = 0; i < state.count; i++) {
            if (strcmp(state.entries[i].name, "file2.c") == 0) {
                TEST_ASSERT(strcmp(state.entries[i].extension, "c") == 0, "Should extract .c extension");
                found_c_file = true;
                break;
            }
        }
        TEST_ASSERT(found_c_file, "Should find file2.c");

        directory_state_free(&state);
    }

    // Test: directory_go_parent
    {
        DirectoryState state;
        directory_state_init(&state);

        char subdir_path[512];
        snprintf(subdir_path, sizeof(subdir_path), "%s/subdir1", test_dir);

        directory_read(&state, subdir_path);
        TEST_ASSERT(strcmp(state.current_path, subdir_path) == 0, "Should be in subdir");

        bool result = directory_go_parent(&state);
        TEST_ASSERT(result, "Should navigate to parent");
        TEST_ASSERT(strcmp(state.current_path, test_dir) == 0, "Should be in test dir");

        directory_state_free(&state);
    }

    // Test: directory_enter
    {
        DirectoryState state;
        directory_state_init(&state);

        directory_read(&state, test_dir);

        // Find subdir1 index
        int subdir_index = -1;
        for (int i = 0; i < state.count; i++) {
            if (strcmp(state.entries[i].name, "subdir1") == 0) {
                subdir_index = i;
                break;
            }
        }

        TEST_ASSERT(subdir_index >= 0, "Should find subdir1");

        if (subdir_index >= 0) {
            bool result = directory_enter(&state, subdir_index);
            TEST_ASSERT(result, "Should enter subdirectory");

            char expected_path[512];
            snprintf(expected_path, sizeof(expected_path), "%s/subdir1", test_dir);
            TEST_ASSERT(strcmp(state.current_path, expected_path) == 0, "Should be in subdir1");
        }

        directory_state_free(&state);
    }

    // Test: format_file_size
    {
        char buffer[32];

        format_file_size(0, buffer, sizeof(buffer));
        TEST_ASSERT(strcmp(buffer, "0 B") == 0, "Should format 0 bytes");

        format_file_size(512, buffer, sizeof(buffer));
        TEST_ASSERT(strcmp(buffer, "512 B") == 0, "Should format bytes");

        format_file_size(1024, buffer, sizeof(buffer));
        TEST_ASSERT(strstr(buffer, "KB") != NULL, "Should format as KB");

        format_file_size(1024 * 1024, buffer, sizeof(buffer));
        TEST_ASSERT(strstr(buffer, "MB") != NULL, "Should format as MB");

        format_file_size(1024LL * 1024 * 1024, buffer, sizeof(buffer));
        TEST_ASSERT(strstr(buffer, "GB") != NULL, "Should format as GB");
    }

    // Test: get_free_disk_space
    {
        off_t free_space = get_free_disk_space("/");
        TEST_ASSERT(free_space > 0, "Should get positive free space");
    }

    // Test: directory_toggle_hidden
    {
        DirectoryState state;
        directory_state_init(&state);

        TEST_ASSERT(!state.show_hidden, "show_hidden should start false");

        directory_read(&state, test_dir);
        int count_without_hidden = state.count;

        directory_toggle_hidden(&state);
        TEST_ASSERT(state.show_hidden, "show_hidden should be true after toggle");
        TEST_ASSERT(state.count > count_without_hidden, "Should have more files with hidden shown");

        directory_state_free(&state);
    }

    // Test: handles root directory
    {
        DirectoryState state;
        directory_state_init(&state);

        bool result = directory_read(&state, "/");
        TEST_ASSERT(result, "Should read root directory");
        TEST_ASSERT(state.count > 0, "Root should have entries");

        // Can't go parent from root
        result = directory_go_parent(&state);
        TEST_ASSERT(!result, "Should not go parent from root");

        directory_state_free(&state);
    }

    printf("  Cleaning up test environment...\n");
    teardown_test_dir();
}

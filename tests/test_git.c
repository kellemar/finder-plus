#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "core/git.h"

// External test macros from test_main.c
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

// Test directory path
static const char *TEST_DIR = "/tmp/finder_plus_git_test";

// Helper to create a test git repo
static bool setup_test_git_repo(void)
{
    char cmd[1024];

    // Clean up any existing test directory
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);

    // Create test directory
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", TEST_DIR);
    if (system(cmd) != 0) return false;

    // Initialize git repo
    snprintf(cmd, sizeof(cmd), "cd %s && git init -q", TEST_DIR);
    if (system(cmd) != 0) return false;

    // Configure git user (required for commits)
    snprintf(cmd, sizeof(cmd), "cd %s && git config user.email 'test@test.com' && git config user.name 'Test'", TEST_DIR);
    if (system(cmd) != 0) return false;

    // Create some test files
    snprintf(cmd, sizeof(cmd), "echo 'test' > %s/file1.txt", TEST_DIR);
    if (system(cmd) != 0) return false;

    snprintf(cmd, sizeof(cmd), "echo 'test2' > %s/file2.txt", TEST_DIR);
    if (system(cmd) != 0) return false;

    return true;
}

static void teardown_test_git_repo(void)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

// Test git_is_repo
static void test_git_is_repo(void)
{
    printf("  Testing git_is_repo...\n");

    TEST_ASSERT(git_is_repo(TEST_DIR) == true, "Test directory should be a git repo");
    TEST_ASSERT(git_is_repo("/tmp") == false, "/tmp should not be a git repo");
}

// Test git_get_repo_root
static void test_git_get_repo_root(void)
{
    printf("  Testing git_get_repo_root...\n");

    char root[GIT_PATH_MAX_LEN];
    bool result = git_get_repo_root(TEST_DIR, root, sizeof(root));

    TEST_ASSERT(result == true, "Should get repo root");
    TEST_ASSERT(strstr(root, "finder_plus_git_test") != NULL, "Root should contain test dir name");
}

// Test git_get_branch
static void test_git_get_branch(void)
{
    printf("  Testing git_get_branch...\n");

    // First make an initial commit so we have a branch
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd %s && git add . && git commit -q -m 'Initial commit'", TEST_DIR);
    system(cmd);

    char branch[GIT_BRANCH_MAX_LEN];
    bool result = git_get_branch(TEST_DIR, branch, sizeof(branch));

    TEST_ASSERT(result == true, "Should get branch name");
    // Could be "main" or "master" depending on git config
    TEST_ASSERT(strlen(branch) > 0, "Branch name should not be empty");
}

// Test git_update_state
static void test_git_update_state(void)
{
    printf("  Testing git_update_state...\n");

    GitState state;
    git_state_init(&state);

    bool result = git_update_state(&state, TEST_DIR);

    TEST_ASSERT(result == true, "Should update git state");
    TEST_ASSERT(state.is_repo == true, "Should be marked as repo");
    TEST_ASSERT(strlen(state.branch) > 0, "Should have branch name");

    git_state_free(&state);
}

// Test git_get_status with various file states
static void test_git_get_status(void)
{
    printf("  Testing git_get_status...\n");

    // Create an untracked file
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "echo 'new content' > %s/untracked.txt", TEST_DIR);
    system(cmd);

    // Modify a committed file
    snprintf(cmd, sizeof(cmd), "echo 'modified' >> %s/file1.txt", TEST_DIR);
    system(cmd);

    // Stage a file
    snprintf(cmd, sizeof(cmd), "cd %s && echo 'staged' > staged.txt && git add staged.txt", TEST_DIR);
    system(cmd);

    GitStatusResult result;
    git_status_result_init(&result);

    bool status_ok = git_get_status(TEST_DIR, &result);

    TEST_ASSERT(status_ok == true, "Should get git status");
    TEST_ASSERT(result.count >= 2, "Should have at least 2 status entries");

    // Check for untracked file
    GitFileStatus untracked_status = git_get_file_status(&result, "untracked.txt");
    TEST_ASSERT(untracked_status == GIT_STATUS_UNTRACKED, "untracked.txt should be untracked");

    // Check for modified file
    GitFileStatus modified_status = git_get_file_status(&result, "file1.txt");
    TEST_ASSERT(modified_status == GIT_STATUS_MODIFIED, "file1.txt should be modified");

    // Check for staged file
    GitFileStatus staged_status = git_get_file_status(&result, "staged.txt");
    TEST_ASSERT(staged_status == GIT_STATUS_STAGED, "staged.txt should be staged");

    git_status_result_free(&result);
}

// Test git_status_char
static void test_git_status_char(void)
{
    printf("  Testing git_status_char...\n");

    TEST_ASSERT(git_status_char(GIT_STATUS_MODIFIED) == 'M', "Modified should be M");
    TEST_ASSERT(git_status_char(GIT_STATUS_STAGED) == 'A', "Staged should be A");
    TEST_ASSERT(git_status_char(GIT_STATUS_UNTRACKED) == '?', "Untracked should be ?");
    TEST_ASSERT(git_status_char(GIT_STATUS_DELETED) == 'D', "Deleted should be D");
    TEST_ASSERT(git_status_char(GIT_STATUS_CONFLICT) == 'U', "Conflict should be U");
    TEST_ASSERT(git_status_char(GIT_STATUS_NONE) == ' ', "None should be space");
}

// Test git_status_string
static void test_git_status_string(void)
{
    printf("  Testing git_status_string...\n");

    TEST_ASSERT_STR_EQ("Modified", git_status_string(GIT_STATUS_MODIFIED), "Modified string");
    TEST_ASSERT_STR_EQ("Staged", git_status_string(GIT_STATUS_STAGED), "Staged string");
    TEST_ASSERT_STR_EQ("Untracked", git_status_string(GIT_STATUS_UNTRACKED), "Untracked string");
    TEST_ASSERT_STR_EQ("Deleted", git_status_string(GIT_STATUS_DELETED), "Deleted string");
    TEST_ASSERT_STR_EQ("Conflict", git_status_string(GIT_STATUS_CONFLICT), "Conflict string");
    TEST_ASSERT_STR_EQ("", git_status_string(GIT_STATUS_NONE), "None string");
}

// Test git_stage_file and git_unstage_file
static void test_git_stage_unstage(void)
{
    printf("  Testing git_stage_file and git_unstage_file...\n");

    // Create a new file
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "echo 'to stage' > %s/to_stage.txt", TEST_DIR);
    system(cmd);

    // Stage the file
    bool stage_result = git_stage_file(TEST_DIR, "to_stage.txt");
    TEST_ASSERT(stage_result == true, "Should stage file");

    // Verify it's staged
    GitStatusResult result;
    git_status_result_init(&result);
    git_get_status(TEST_DIR, &result);
    GitFileStatus status = git_get_file_status(&result, "to_stage.txt");
    TEST_ASSERT(status == GIT_STATUS_STAGED, "File should be staged");
    git_status_result_free(&result);

    // Unstage the file
    bool unstage_result = git_unstage_file(TEST_DIR, "to_stage.txt");
    TEST_ASSERT(unstage_result == true, "Should unstage file");

    // Verify it's untracked again
    git_status_result_init(&result);
    git_get_status(TEST_DIR, &result);
    status = git_get_file_status(&result, "to_stage.txt");
    TEST_ASSERT(status == GIT_STATUS_UNTRACKED, "File should be untracked after unstage");
    git_status_result_free(&result);
}

// Test state initialization and cleanup
static void test_git_state_lifecycle(void)
{
    printf("  Testing git state lifecycle...\n");

    GitState state;
    git_state_init(&state);

    TEST_ASSERT(state.is_repo == false, "Initial state should not be repo");
    TEST_ASSERT(state.branch[0] == '\0', "Initial branch should be empty");
    TEST_ASSERT(state.has_staged == false, "Initial has_staged should be false");
    TEST_ASSERT(state.has_modified == false, "Initial has_modified should be false");

    git_state_free(&state);

    // After free, should be zeroed
    TEST_ASSERT(state.is_repo == false, "After free, is_repo should be false");
}

// Test GitStatusResult lifecycle
static void test_git_status_result_lifecycle(void)
{
    printf("  Testing git status result lifecycle...\n");

    GitStatusResult result;
    git_status_result_init(&result);

    TEST_ASSERT(result.entries == NULL, "Initial entries should be NULL");
    TEST_ASSERT(result.count == 0, "Initial count should be 0");
    TEST_ASSERT(result.capacity == 0, "Initial capacity should be 0");

    git_status_result_free(&result);

    TEST_ASSERT(result.entries == NULL, "After free, entries should be NULL");
    TEST_ASSERT(result.count == 0, "After free, count should be 0");
}

// Test non-repo directory
static void test_git_non_repo(void)
{
    printf("  Testing non-repo directory...\n");

    GitState state;
    git_state_init(&state);

    bool result = git_update_state(&state, "/tmp");

    TEST_ASSERT(result == false, "Should fail for non-repo");
    TEST_ASSERT(state.is_repo == false, "Should not be marked as repo");

    git_state_free(&state);
}

// Main test function
void test_git(void)
{
    printf("\n  Setting up git test repo...\n");
    if (!setup_test_git_repo()) {
        printf("  SKIP: Failed to set up git test repo (git may not be installed)\n");
        return;
    }

    test_git_state_lifecycle();
    test_git_status_result_lifecycle();
    test_git_is_repo();
    test_git_get_repo_root();
    test_git_get_branch();
    test_git_update_state();
    test_git_get_status();
    test_git_status_char();
    test_git_status_string();
    test_git_stage_unstage();
    test_git_non_repo();

    printf("  Cleaning up git test repo...\n");
    teardown_test_git_repo();
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "core/operation_queue.h"

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
static const char *TEST_DIR = "/tmp/finder_plus_queue_test";

// Helper to set up test directory
static bool setup_test_directory(void)
{
    char cmd[1024];

    // Clean up any existing test directory
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);

    // Create test directory
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", TEST_DIR);
    if (system(cmd) != 0) return false;

    // Create some test files
    snprintf(cmd, sizeof(cmd), "echo 'test content 1' > %s/file1.txt", TEST_DIR);
    if (system(cmd) != 0) return false;

    snprintf(cmd, sizeof(cmd), "echo 'test content 2' > %s/file2.txt", TEST_DIR);
    if (system(cmd) != 0) return false;

    snprintf(cmd, sizeof(cmd), "mkdir -p %s/subdir", TEST_DIR);
    if (system(cmd) != 0) return false;

    return true;
}

static void teardown_test_directory(void)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

// Test queue initialization
static void test_queue_init(void)
{
    printf("  Testing operation_queue_init...\n");

    OperationQueue queue;
    operation_queue_init(&queue);

    TEST_ASSERT_EQ(0, queue.count, "Initial count should be 0");
    TEST_ASSERT_EQ(-1, queue.current_index, "Initial current_index should be -1");
    TEST_ASSERT_EQ(1, queue.next_id, "Initial next_id should be 1");
    TEST_ASSERT(queue.is_paused == false, "Initial is_paused should be false");
    TEST_ASSERT(queue.is_processing == false, "Initial is_processing should be false");
    TEST_ASSERT(queue.worker_running == false, "Initial worker_running should be false");

    operation_queue_free(&queue);
}

// Test adding operations
static void test_queue_add_operations(void)
{
    printf("  Testing adding operations to queue...\n");

    OperationQueue queue;
    operation_queue_init(&queue);

    char source[512], dest[512];
    snprintf(source, sizeof(source), "%s/file1.txt", TEST_DIR);
    snprintf(dest, sizeof(dest), "%s/file1_copy.txt", TEST_DIR);

    // Add copy operation
    int id1 = operation_queue_copy(&queue, source, dest);
    TEST_ASSERT(id1 > 0, "Copy operation should return valid ID");
    TEST_ASSERT_EQ(1, queue.count, "Queue should have 1 operation");

    // Add delete operation
    snprintf(source, sizeof(source), "%s/file2.txt", TEST_DIR);
    int id2 = operation_queue_delete(&queue, source);
    TEST_ASSERT(id2 > 0, "Delete operation should return valid ID");
    TEST_ASSERT(id2 != id1, "Each operation should have unique ID");
    TEST_ASSERT_EQ(2, queue.count, "Queue should have 2 operations");

    // Verify operation details
    QueuedOperation *op = operation_queue_get(&queue, id1);
    TEST_ASSERT(op != NULL, "Should get operation by ID");
    TEST_ASSERT_EQ(QUEUE_OP_COPY, op->type, "Operation type should be COPY");
    TEST_ASSERT_EQ(OP_STATUS_PENDING, op->status, "Operation status should be PENDING");

    operation_queue_free(&queue);
}

// Test operation type names
static void test_queue_op_type_names(void)
{
    printf("  Testing queue_op_type_name...\n");

    TEST_ASSERT_STR_EQ("Copy", queue_op_type_name(QUEUE_OP_COPY), "Copy type name");
    TEST_ASSERT_STR_EQ("Move", queue_op_type_name(QUEUE_OP_MOVE), "Move type name");
    TEST_ASSERT_STR_EQ("Delete", queue_op_type_name(QUEUE_OP_DELETE), "Delete type name");
    TEST_ASSERT_STR_EQ("Rename", queue_op_type_name(QUEUE_OP_RENAME), "Rename type name");
    TEST_ASSERT_STR_EQ("Create Folder", queue_op_type_name(QUEUE_OP_CREATE_DIR), "Create Folder type name");
    TEST_ASSERT_STR_EQ("Duplicate", queue_op_type_name(QUEUE_OP_DUPLICATE), "Duplicate type name");
}

// Test operation status names
static void test_operation_status_names(void)
{
    printf("  Testing operation_status_name...\n");

    TEST_ASSERT_STR_EQ("Pending", operation_status_name(OP_STATUS_PENDING), "Pending status name");
    TEST_ASSERT_STR_EQ("In Progress", operation_status_name(OP_STATUS_IN_PROGRESS), "In Progress status name");
    TEST_ASSERT_STR_EQ("Completed", operation_status_name(OP_STATUS_COMPLETED), "Completed status name");
    TEST_ASSERT_STR_EQ("Failed", operation_status_name(OP_STATUS_FAILED), "Failed status name");
    TEST_ASSERT_STR_EQ("Cancelled", operation_status_name(OP_STATUS_CANCELLED), "Cancelled status name");
}

// Test queue pause/resume
static void test_queue_pause_resume(void)
{
    printf("  Testing queue pause/resume...\n");

    OperationQueue queue;
    operation_queue_init(&queue);

    TEST_ASSERT(operation_queue_is_paused(&queue) == false, "Queue should not be paused initially");

    operation_queue_pause(&queue);
    TEST_ASSERT(operation_queue_is_paused(&queue) == true, "Queue should be paused after pause()");

    operation_queue_resume(&queue);
    TEST_ASSERT(operation_queue_is_paused(&queue) == false, "Queue should not be paused after resume()");

    operation_queue_free(&queue);
}

// Test cancelling operations
static void test_queue_cancel(void)
{
    printf("  Testing queue cancel...\n");

    OperationQueue queue;
    operation_queue_init(&queue);

    char source[512], dest[512];
    snprintf(source, sizeof(source), "%s/file1.txt", TEST_DIR);
    snprintf(dest, sizeof(dest), "%s/file1_copy.txt", TEST_DIR);

    int id = operation_queue_copy(&queue, source, dest);

    // Cancel the operation
    bool cancelled = operation_queue_cancel(&queue, id);
    TEST_ASSERT(cancelled == true, "Should successfully cancel pending operation");

    QueuedOperation *op = operation_queue_get(&queue, id);
    TEST_ASSERT(op != NULL, "Operation should still exist");
    TEST_ASSERT_EQ(OP_STATUS_CANCELLED, op->status, "Operation status should be CANCELLED");

    // Try to cancel again (should fail since already cancelled)
    cancelled = operation_queue_cancel(&queue, id);
    TEST_ASSERT(cancelled == false, "Should not cancel already cancelled operation");

    operation_queue_free(&queue);
}

// Test cancel all
static void test_queue_cancel_all(void)
{
    printf("  Testing queue cancel all...\n");

    OperationQueue queue;
    operation_queue_init(&queue);

    char source[512], dest[512];

    // Add multiple operations
    snprintf(source, sizeof(source), "%s/file1.txt", TEST_DIR);
    snprintf(dest, sizeof(dest), "%s/file1_copy.txt", TEST_DIR);
    int id1 = operation_queue_copy(&queue, source, dest);

    snprintf(source, sizeof(source), "%s/file2.txt", TEST_DIR);
    snprintf(dest, sizeof(dest), "%s/file2_copy.txt", TEST_DIR);
    int id2 = operation_queue_copy(&queue, source, dest);

    operation_queue_cancel_all(&queue);

    QueuedOperation *op1 = operation_queue_get(&queue, id1);
    QueuedOperation *op2 = operation_queue_get(&queue, id2);

    TEST_ASSERT_EQ(OP_STATUS_CANCELLED, op1->status, "First operation should be cancelled");
    TEST_ASSERT_EQ(OP_STATUS_CANCELLED, op2->status, "Second operation should be cancelled");

    operation_queue_free(&queue);
}

// Test queue counts
static void test_queue_counts(void)
{
    printf("  Testing queue counts...\n");

    OperationQueue queue;
    operation_queue_init(&queue);

    TEST_ASSERT_EQ(0, operation_queue_total_count(&queue), "Initial total count should be 0");
    TEST_ASSERT_EQ(0, operation_queue_pending_count(&queue), "Initial pending count should be 0");
    TEST_ASSERT(operation_queue_is_empty(&queue) == true, "Queue should be empty initially");

    char source[512], dest[512];
    snprintf(source, sizeof(source), "%s/file1.txt", TEST_DIR);
    snprintf(dest, sizeof(dest), "%s/file1_copy.txt", TEST_DIR);

    operation_queue_copy(&queue, source, dest);

    TEST_ASSERT_EQ(1, operation_queue_total_count(&queue), "Total count should be 1");
    TEST_ASSERT_EQ(1, operation_queue_pending_count(&queue), "Pending count should be 1");
    TEST_ASSERT(operation_queue_is_empty(&queue) == false, "Queue should not be empty");

    operation_queue_free(&queue);
}

// Test clear finished operations
static void test_queue_clear_finished(void)
{
    printf("  Testing queue clear finished...\n");

    OperationQueue queue;
    operation_queue_init(&queue);

    char source[512], dest[512];

    // Add and cancel an operation
    snprintf(source, sizeof(source), "%s/file1.txt", TEST_DIR);
    snprintf(dest, sizeof(dest), "%s/file1_copy.txt", TEST_DIR);
    int id1 = operation_queue_copy(&queue, source, dest);
    operation_queue_cancel(&queue, id1);

    // Add a pending operation
    snprintf(source, sizeof(source), "%s/file2.txt", TEST_DIR);
    snprintf(dest, sizeof(dest), "%s/file2_copy.txt", TEST_DIR);
    operation_queue_copy(&queue, source, dest);

    TEST_ASSERT_EQ(2, queue.count, "Should have 2 operations");

    operation_queue_clear_finished(&queue);

    TEST_ASSERT_EQ(1, queue.count, "Should have 1 operation after clearing finished");
    TEST_ASSERT_EQ(1, operation_queue_pending_count(&queue), "Should have 1 pending operation");

    operation_queue_free(&queue);
}

// Test queue worker thread
static void test_queue_worker(void)
{
    printf("  Testing queue worker thread...\n");

    OperationQueue queue;
    operation_queue_init(&queue);

    // Start worker thread
    bool started = operation_queue_start(&queue);
    TEST_ASSERT(started == true, "Worker thread should start");
    TEST_ASSERT(queue.worker_running == true, "Worker should be marked as running");

    // Add a copy operation - dest should be directory, not full path
    char source[512], dest_dir[512], expected_dest[512];
    snprintf(source, sizeof(source), "%s/file1.txt", TEST_DIR);
    snprintf(dest_dir, sizeof(dest_dir), "%s/subdir", TEST_DIR);  // Copy to subdir
    snprintf(expected_dest, sizeof(expected_dest), "%s/subdir/file1.txt", TEST_DIR);

    int id = operation_queue_copy(&queue, source, dest_dir);
    TEST_ASSERT(id > 0, "Should add copy operation");

    // Wait for operation to complete (with timeout)
    int wait_count = 0;
    while (wait_count < 50) { // Max 5 seconds
        QueuedOperation *op = operation_queue_get(&queue, id);
        if (op && (op->status == OP_STATUS_COMPLETED || op->status == OP_STATUS_FAILED)) {
            break;
        }
        usleep(100000); // 100ms
        wait_count++;
    }

    QueuedOperation *op = operation_queue_get(&queue, id);
    TEST_ASSERT(op != NULL, "Operation should exist");
    TEST_ASSERT_EQ(OP_STATUS_COMPLETED, op->status, "Operation should complete");
    TEST_ASSERT_EQ(100, op->progress, "Progress should be 100");

    // Verify file was copied to destination directory (with original filename)
    struct stat st;
    int result = stat(expected_dest, &st);
    TEST_ASSERT(result == 0, "Destination file should exist");

    // Stop worker thread
    operation_queue_stop(&queue);
    TEST_ASSERT(queue.worker_running == false, "Worker should be stopped");

    operation_queue_free(&queue);
}

// Test move operation
static void test_queue_move_operation(void)
{
    printf("  Testing move operation...\n");

    OperationQueue queue;
    operation_queue_init(&queue);
    operation_queue_start(&queue);

    // file_move expects dest to be a directory, not full path
    char source[512], dest_dir[512], expected_dest[512];
    snprintf(source, sizeof(source), "%s/file1.txt", TEST_DIR);
    snprintf(dest_dir, sizeof(dest_dir), "%s/subdir", TEST_DIR);  // Move to subdir
    snprintf(expected_dest, sizeof(expected_dest), "%s/subdir/file1.txt", TEST_DIR);

    // Check source exists
    struct stat st;
    TEST_ASSERT(stat(source, &st) == 0, "Source file should exist before move");

    int id = operation_queue_move(&queue, source, dest_dir);

    // Wait for completion
    int wait_count = 0;
    while (wait_count < 50) {
        QueuedOperation *op = operation_queue_get(&queue, id);
        if (op && (op->status == OP_STATUS_COMPLETED || op->status == OP_STATUS_FAILED)) {
            break;
        }
        usleep(100000);
        wait_count++;
    }

    QueuedOperation *op = operation_queue_get(&queue, id);
    TEST_ASSERT_EQ(OP_STATUS_COMPLETED, op->status, "Move operation should complete");

    // Verify file was moved
    TEST_ASSERT(stat(source, &st) != 0, "Source should not exist after move");
    TEST_ASSERT(stat(expected_dest, &st) == 0, "Destination should exist after move");

    operation_queue_stop(&queue);
    operation_queue_free(&queue);
}

// Test create directory operation
static void test_queue_create_dir_operation(void)
{
    printf("  Testing create directory operation...\n");

    OperationQueue queue;
    operation_queue_init(&queue);
    operation_queue_start(&queue);

    int id = operation_queue_create_dir(&queue, TEST_DIR, "new_folder");

    // Wait for completion
    int wait_count = 0;
    while (wait_count < 50) {
        QueuedOperation *op = operation_queue_get(&queue, id);
        if (op && (op->status == OP_STATUS_COMPLETED || op->status == OP_STATUS_FAILED)) {
            break;
        }
        usleep(100000);
        wait_count++;
    }

    QueuedOperation *op = operation_queue_get(&queue, id);
    TEST_ASSERT_EQ(OP_STATUS_COMPLETED, op->status, "Create dir operation should complete");

    // Verify directory was created
    char path[512];
    snprintf(path, sizeof(path), "%s/new_folder", TEST_DIR);
    struct stat st;
    TEST_ASSERT(stat(path, &st) == 0, "Directory should exist");
    TEST_ASSERT(S_ISDIR(st.st_mode), "Should be a directory");

    operation_queue_stop(&queue);
    operation_queue_free(&queue);
}

// Test overall progress
static void test_queue_overall_progress(void)
{
    printf("  Testing overall progress...\n");

    OperationQueue queue;
    operation_queue_init(&queue);

    // Empty queue should return 100%
    TEST_ASSERT_EQ(100, operation_queue_overall_progress(&queue), "Empty queue should show 100% progress");

    // Add pending operation
    char source[512], dest[512];
    snprintf(source, sizeof(source), "%s/file2.txt", TEST_DIR);
    snprintf(dest, sizeof(dest), "%s/file2_progress.txt", TEST_DIR);

    operation_queue_copy(&queue, source, dest);

    // Pending operation has 0 progress
    TEST_ASSERT_EQ(0, operation_queue_overall_progress(&queue), "Queue with pending operation should show 0% progress");

    operation_queue_free(&queue);
}

// Test retry operation
static void test_queue_retry(void)
{
    printf("  Testing retry operation...\n");

    OperationQueue queue;
    operation_queue_init(&queue);

    char source[512], dest[512];
    snprintf(source, sizeof(source), "%s/nonexistent.txt", TEST_DIR);
    snprintf(dest, sizeof(dest), "%s/copy.txt", TEST_DIR);

    // This operation should fail since source doesn't exist
    operation_queue_start(&queue);
    int id = operation_queue_copy(&queue, source, dest);

    // Wait for failure
    int wait_count = 0;
    while (wait_count < 50) {
        QueuedOperation *op = operation_queue_get(&queue, id);
        if (op && (op->status == OP_STATUS_COMPLETED || op->status == OP_STATUS_FAILED)) {
            break;
        }
        usleep(100000);
        wait_count++;
    }

    QueuedOperation *op = operation_queue_get(&queue, id);
    TEST_ASSERT_EQ(OP_STATUS_FAILED, op->status, "Operation should fail");
    TEST_ASSERT(op->can_retry == true, "Failed operation should be retryable");
    TEST_ASSERT(strlen(op->error_message) > 0, "Failed operation should have error message");

    // Try retry
    bool retried = operation_queue_retry(&queue, id);
    TEST_ASSERT(retried == true, "Should be able to retry failed operation");

    op = operation_queue_get(&queue, id);
    TEST_ASSERT_EQ(OP_STATUS_PENDING, op->status, "After retry, status should be PENDING");

    operation_queue_stop(&queue);
    operation_queue_free(&queue);
}

// Main test function
void test_operation_queue(void)
{
    printf("\n  Setting up operation queue test directory...\n");
    if (!setup_test_directory()) {
        printf("  SKIP: Failed to set up test directory\n");
        return;
    }

    test_queue_init();
    test_queue_add_operations();
    test_queue_op_type_names();
    test_operation_status_names();
    test_queue_pause_resume();
    test_queue_cancel();
    test_queue_cancel_all();
    test_queue_counts();
    test_queue_clear_finished();
    test_queue_overall_progress();
    test_queue_worker();
    test_queue_retry();

    // Re-setup for remaining tests that modify files
    teardown_test_directory();
    setup_test_directory();

    test_queue_move_operation();

    // Re-setup again
    teardown_test_directory();
    setup_test_directory();

    test_queue_create_dir_operation();

    printf("  Cleaning up operation queue test directory...\n");
    teardown_test_directory();
}

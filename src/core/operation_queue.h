#ifndef OPERATION_QUEUE_H
#define OPERATION_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>

#define QUEUE_MAX_OPERATIONS 256
#define QUEUE_PATH_MAX_LEN 4096
#define QUEUE_MAX_HISTORY 64

// Queue operation types (distinct from clipboard OperationType in operations.h)
typedef enum QueueOpType {
    QUEUE_OP_COPY,
    QUEUE_OP_MOVE,
    QUEUE_OP_DELETE,
    QUEUE_OP_RENAME,
    QUEUE_OP_CREATE_DIR,
    QUEUE_OP_DUPLICATE
} QueueOpType;

// Operation status
typedef enum OperationStatus {
    OP_STATUS_PENDING,
    OP_STATUS_IN_PROGRESS,
    OP_STATUS_COMPLETED,
    OP_STATUS_FAILED,
    OP_STATUS_CANCELLED
} OperationStatus;

// Single queued operation
typedef struct QueuedOperation {
    int id;                                 // Unique operation ID
    QueueOpType type;
    OperationStatus status;
    char source_path[QUEUE_PATH_MAX_LEN];   // Source file/folder
    char dest_path[QUEUE_PATH_MAX_LEN];     // Destination (for copy/move/rename)
    off_t total_bytes;                      // Total bytes to process
    off_t processed_bytes;                  // Bytes processed so far
    int progress;                           // Progress 0-100
    char error_message[256];                // Error message if failed
    time_t created_at;                      // When operation was created
    time_t started_at;                      // When operation started
    time_t completed_at;                    // When operation completed
    bool can_retry;                         // Can this operation be retried
} QueuedOperation;

// Operation queue state
typedef struct OperationQueue {
    QueuedOperation operations[QUEUE_MAX_OPERATIONS];
    int count;
    int next_id;
    bool is_paused;
    bool is_processing;

    // Current operation being processed
    int current_index;

    // History of completed operations
    QueuedOperation history[QUEUE_MAX_HISTORY];
    int history_count;

    // Thread synchronization
    pthread_mutex_t mutex;
    pthread_t worker_thread;
    bool worker_running;
    bool should_stop;
} OperationQueue;

// Initialize operation queue
void operation_queue_init(OperationQueue *queue);

// Free operation queue resources
void operation_queue_free(OperationQueue *queue);

// Start the background worker thread
bool operation_queue_start(OperationQueue *queue);

// Stop the background worker thread
void operation_queue_stop(OperationQueue *queue);

// Add a copy operation to the queue
int operation_queue_copy(OperationQueue *queue, const char *source, const char *dest);

// Add a move operation to the queue
int operation_queue_move(OperationQueue *queue, const char *source, const char *dest);

// Add a delete operation to the queue
int operation_queue_delete(OperationQueue *queue, const char *path);

// Add a rename operation to the queue
int operation_queue_rename(OperationQueue *queue, const char *source, const char *new_name);

// Add a create directory operation to the queue
int operation_queue_create_dir(OperationQueue *queue, const char *parent, const char *name);

// Add a duplicate operation to the queue
int operation_queue_duplicate(OperationQueue *queue, const char *source);

// Pause the queue
void operation_queue_pause(OperationQueue *queue);

// Resume the queue
void operation_queue_resume(OperationQueue *queue);

// Cancel a specific operation
bool operation_queue_cancel(OperationQueue *queue, int operation_id);

// Cancel all pending operations
void operation_queue_cancel_all(OperationQueue *queue);

// Retry a failed operation
bool operation_queue_retry(OperationQueue *queue, int operation_id);

// Remove completed/cancelled/failed operations from queue
void operation_queue_clear_finished(OperationQueue *queue);

// Get operation by ID
QueuedOperation* operation_queue_get(OperationQueue *queue, int operation_id);

// Get current operation being processed
QueuedOperation* operation_queue_current(OperationQueue *queue);

// Get pending count
int operation_queue_pending_count(OperationQueue *queue);

// Get total count
int operation_queue_total_count(OperationQueue *queue);

// Check if queue is empty
bool operation_queue_is_empty(OperationQueue *queue);

// Check if queue is paused
bool operation_queue_is_paused(OperationQueue *queue);

// Check if queue is processing
bool operation_queue_is_processing(OperationQueue *queue);

// Get overall progress (0-100)
int operation_queue_overall_progress(OperationQueue *queue);

// Get operation type name
const char* queue_op_type_name(QueueOpType type);

// Get operation status name
const char* operation_status_name(OperationStatus status);

#endif // OPERATION_QUEUE_H

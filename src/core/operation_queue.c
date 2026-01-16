#include "operation_queue.h"
#include "operations.h"
#include "filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

// Get file size for progress tracking
static off_t get_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

// Get directory total size recursively
static off_t get_dir_size(const char *path)
{
    off_t total = 0;
    DIR *dir = opendir(path);
    if (dir == NULL) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[QUEUE_PATH_MAX_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                total += get_dir_size(full_path);
            } else {
                total += st.st_size;
            }
        }
    }

    closedir(dir);
    return total;
}

// Process a single operation
static bool process_operation(QueuedOperation *op)
{
    op->status = OP_STATUS_IN_PROGRESS;
    op->started_at = time(NULL);

    OperationResult result = OP_ERROR_UNKNOWN;

    switch (op->type) {
        case QUEUE_OP_COPY:
            result = file_copy(op->source_path, op->dest_path);
            break;

        case QUEUE_OP_MOVE:
            result = file_move(op->source_path, op->dest_path);
            break;

        case QUEUE_OP_DELETE:
            result = file_delete(op->source_path);
            break;

        case QUEUE_OP_RENAME:
            result = file_rename(op->source_path, op->dest_path);
            break;

        case QUEUE_OP_CREATE_DIR: {
            // Extract parent and name from dest_path
            char parent[QUEUE_PATH_MAX_LEN];
            char *last_slash = strrchr(op->dest_path, '/');
            if (last_slash != NULL) {
                size_t parent_len = last_slash - op->dest_path;
                strncpy(parent, op->dest_path, parent_len);
                parent[parent_len] = '\0';
                result = file_create_directory(parent, last_slash + 1);
            } else {
                result = file_create_directory(".", op->dest_path);
            }
            break;
        }

        case QUEUE_OP_DUPLICATE:
            result = file_duplicate(op->source_path);
            break;
    }

    op->completed_at = time(NULL);

    if (result == OP_SUCCESS) {
        op->status = OP_STATUS_COMPLETED;
        op->progress = 100;
        op->processed_bytes = op->total_bytes;
    } else {
        op->status = OP_STATUS_FAILED;
        op->can_retry = true;
        const char *err_msg = operations_get_error();
        if (err_msg && strlen(err_msg) > 0) {
            snprintf(op->error_message, sizeof(op->error_message), "%s", err_msg);
        } else {
            snprintf(op->error_message, sizeof(op->error_message), "Operation failed: %s", strerror(errno));
        }
    }

    return result == OP_SUCCESS;
}

// Worker thread function
static void* worker_thread_func(void *arg)
{
    OperationQueue *queue = (OperationQueue*)arg;

    while (!queue->should_stop) {
        QueuedOperation *next_op = NULL;

        // Lock and find next pending operation
        pthread_mutex_lock(&queue->mutex);

        if (!queue->is_paused && !queue->should_stop) {
            for (int i = 0; i < queue->count; i++) {
                if (queue->operations[i].status == OP_STATUS_PENDING) {
                    next_op = &queue->operations[i];
                    queue->current_index = i;
                    queue->is_processing = true;
                    break;
                }
            }
        }

        pthread_mutex_unlock(&queue->mutex);

        if (next_op != NULL) {
            // Process the operation (outside the lock)
            process_operation(next_op);

            // Add to history
            pthread_mutex_lock(&queue->mutex);

            if (queue->history_count >= QUEUE_MAX_HISTORY) {
                // Shift history
                memmove(&queue->history[0], &queue->history[1],
                        (QUEUE_MAX_HISTORY - 1) * sizeof(QueuedOperation));
                queue->history_count--;
            }
            queue->history[queue->history_count++] = *next_op;
            queue->is_processing = false;
            queue->current_index = -1;

            pthread_mutex_unlock(&queue->mutex);
        } else {
            // No work to do, sleep briefly
            usleep(100000); // 100ms
        }
    }

    return NULL;
}

void operation_queue_init(OperationQueue *queue)
{
    memset(queue, 0, sizeof(OperationQueue));
    queue->current_index = -1;
    queue->next_id = 1;
    pthread_mutex_init(&queue->mutex, NULL);
}

void operation_queue_free(OperationQueue *queue)
{
    operation_queue_stop(queue);
    pthread_mutex_destroy(&queue->mutex);
}

bool operation_queue_start(OperationQueue *queue)
{
    if (queue->worker_running) {
        return true; // Already running
    }

    queue->should_stop = false;
    if (pthread_create(&queue->worker_thread, NULL, worker_thread_func, queue) != 0) {
        return false;
    }

    queue->worker_running = true;
    return true;
}

void operation_queue_stop(OperationQueue *queue)
{
    if (!queue->worker_running) {
        return;
    }

    queue->should_stop = true;
    pthread_join(queue->worker_thread, NULL);
    queue->worker_running = false;
}

// Helper to add an operation
static int add_operation(OperationQueue *queue, QueueOpType type,
                         const char *source, const char *dest)
{
    pthread_mutex_lock(&queue->mutex);

    if (queue->count >= QUEUE_MAX_OPERATIONS) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    QueuedOperation *op = &queue->operations[queue->count];
    memset(op, 0, sizeof(QueuedOperation));

    op->id = queue->next_id++;
    op->type = type;
    op->status = OP_STATUS_PENDING;
    op->created_at = time(NULL);

    strncpy(op->source_path, source, sizeof(op->source_path) - 1);
    if (dest != NULL) {
        strncpy(op->dest_path, dest, sizeof(op->dest_path) - 1);
    }

    // Calculate size for progress tracking
    struct stat st;
    if (stat(source, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            op->total_bytes = get_dir_size(source);
        } else {
            op->total_bytes = st.st_size;
        }
    }

    int id = op->id;
    queue->count++;

    pthread_mutex_unlock(&queue->mutex);

    return id;
}

int operation_queue_copy(OperationQueue *queue, const char *source, const char *dest)
{
    return add_operation(queue, QUEUE_OP_COPY, source, dest);
}

int operation_queue_move(OperationQueue *queue, const char *source, const char *dest)
{
    return add_operation(queue, QUEUE_OP_MOVE, source, dest);
}

int operation_queue_delete(OperationQueue *queue, const char *path)
{
    return add_operation(queue, QUEUE_OP_DELETE, path, NULL);
}

int operation_queue_rename(OperationQueue *queue, const char *source, const char *new_name)
{
    return add_operation(queue, QUEUE_OP_RENAME, source, new_name);
}

int operation_queue_create_dir(OperationQueue *queue, const char *parent, const char *name)
{
    char full_path[QUEUE_PATH_MAX_LEN];
    snprintf(full_path, sizeof(full_path), "%s/%s", parent, name);
    return add_operation(queue, QUEUE_OP_CREATE_DIR, parent, full_path);
}

int operation_queue_duplicate(OperationQueue *queue, const char *source)
{
    return add_operation(queue, QUEUE_OP_DUPLICATE, source, NULL);
}

void operation_queue_pause(OperationQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->is_paused = true;
    pthread_mutex_unlock(&queue->mutex);
}

void operation_queue_resume(OperationQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->is_paused = false;
    pthread_mutex_unlock(&queue->mutex);
}

bool operation_queue_cancel(OperationQueue *queue, int operation_id)
{
    pthread_mutex_lock(&queue->mutex);

    for (int i = 0; i < queue->count; i++) {
        if (queue->operations[i].id == operation_id) {
            if (queue->operations[i].status == OP_STATUS_PENDING) {
                queue->operations[i].status = OP_STATUS_CANCELLED;
                pthread_mutex_unlock(&queue->mutex);
                return true;
            }
            break;
        }
    }

    pthread_mutex_unlock(&queue->mutex);
    return false;
}

void operation_queue_cancel_all(OperationQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);

    for (int i = 0; i < queue->count; i++) {
        if (queue->operations[i].status == OP_STATUS_PENDING) {
            queue->operations[i].status = OP_STATUS_CANCELLED;
        }
    }

    pthread_mutex_unlock(&queue->mutex);
}

bool operation_queue_retry(OperationQueue *queue, int operation_id)
{
    pthread_mutex_lock(&queue->mutex);

    for (int i = 0; i < queue->count; i++) {
        if (queue->operations[i].id == operation_id) {
            if (queue->operations[i].status == OP_STATUS_FAILED &&
                queue->operations[i].can_retry) {
                queue->operations[i].status = OP_STATUS_PENDING;
                queue->operations[i].error_message[0] = '\0';
                queue->operations[i].progress = 0;
                queue->operations[i].processed_bytes = 0;
                pthread_mutex_unlock(&queue->mutex);
                return true;
            }
            break;
        }
    }

    pthread_mutex_unlock(&queue->mutex);
    return false;
}

void operation_queue_clear_finished(OperationQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);

    int write_idx = 0;
    for (int read_idx = 0; read_idx < queue->count; read_idx++) {
        OperationStatus status = queue->operations[read_idx].status;
        if (status == OP_STATUS_PENDING || status == OP_STATUS_IN_PROGRESS) {
            if (write_idx != read_idx) {
                queue->operations[write_idx] = queue->operations[read_idx];
            }
            write_idx++;
        }
    }
    queue->count = write_idx;

    pthread_mutex_unlock(&queue->mutex);
}

QueuedOperation* operation_queue_get(OperationQueue *queue, int operation_id)
{
    pthread_mutex_lock(&queue->mutex);

    for (int i = 0; i < queue->count; i++) {
        if (queue->operations[i].id == operation_id) {
            pthread_mutex_unlock(&queue->mutex);
            return &queue->operations[i];
        }
    }

    pthread_mutex_unlock(&queue->mutex);
    return NULL;
}

QueuedOperation* operation_queue_current(OperationQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);

    QueuedOperation *current = NULL;
    if (queue->current_index >= 0 && queue->current_index < queue->count) {
        current = &queue->operations[queue->current_index];
    }

    pthread_mutex_unlock(&queue->mutex);
    return current;
}

int operation_queue_pending_count(OperationQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);

    int count = 0;
    for (int i = 0; i < queue->count; i++) {
        if (queue->operations[i].status == OP_STATUS_PENDING) {
            count++;
        }
    }

    pthread_mutex_unlock(&queue->mutex);
    return count;
}

int operation_queue_total_count(OperationQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    int count = queue->count;
    pthread_mutex_unlock(&queue->mutex);
    return count;
}

bool operation_queue_is_empty(OperationQueue *queue)
{
    return operation_queue_total_count(queue) == 0;
}

bool operation_queue_is_paused(OperationQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    bool paused = queue->is_paused;
    pthread_mutex_unlock(&queue->mutex);
    return paused;
}

bool operation_queue_is_processing(OperationQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    bool processing = queue->is_processing;
    pthread_mutex_unlock(&queue->mutex);
    return processing;
}

int operation_queue_overall_progress(OperationQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);

    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return 100;
    }

    int total_progress = 0;
    for (int i = 0; i < queue->count; i++) {
        total_progress += queue->operations[i].progress;
    }

    pthread_mutex_unlock(&queue->mutex);
    return total_progress / queue->count;
}

const char* queue_op_type_name(QueueOpType type)
{
    switch (type) {
        case QUEUE_OP_COPY:       return "Copy";
        case QUEUE_OP_MOVE:       return "Move";
        case QUEUE_OP_DELETE:     return "Delete";
        case QUEUE_OP_RENAME:     return "Rename";
        case QUEUE_OP_CREATE_DIR: return "Create Folder";
        case QUEUE_OP_DUPLICATE:  return "Duplicate";
        default: return "Unknown";
    }
}

const char* operation_status_name(OperationStatus status)
{
    switch (status) {
        case OP_STATUS_PENDING:     return "Pending";
        case OP_STATUS_IN_PROGRESS: return "In Progress";
        case OP_STATUS_COMPLETED:   return "Completed";
        case OP_STATUS_FAILED:      return "Failed";
        case OP_STATUS_CANCELLED:   return "Cancelled";
        default: return "Unknown";
    }
}

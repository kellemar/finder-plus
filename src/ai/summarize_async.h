#ifndef SUMMARIZE_ASYNC_H
#define SUMMARIZE_ASYNC_H

#include <pthread.h>
#include <stdbool.h>
#include "summarize.h"

// Path length (matches summarize.h)
#define ASYNC_PATH_MAX 1024

// Async summary request structure
typedef struct AsyncSummaryRequest {
    // Input
    char path[ASYNC_PATH_MAX];
    char file_path[ASYNC_PATH_MAX];  // Alternative path field (for context menu)
    SummarizeConfig config;
    SummaryCache *cache;

    // Output (written by worker thread)
    SummaryResult result;
    bool completed;
    bool cancelled;

    // UI flags
    bool from_context_menu;  // If true, show summary in dialog when complete

    // Synchronization
    pthread_mutex_t *mutex;  // Shared with App
} AsyncSummaryRequest;

// Initialize an async summary request
void summarize_async_init(AsyncSummaryRequest *request, pthread_mutex_t *mutex);

// Start async summarization (call from main thread)
// Returns true if thread started successfully
bool summarize_async_start(AsyncSummaryRequest *request, pthread_t *thread,
                           const char *path, SummarizeConfig *config, SummaryCache *cache);

// Cancel async summarization (sets cancelled flag)
void summarize_async_cancel(AsyncSummaryRequest *request);

// Check if async operation completed (call from main thread)
bool summarize_async_is_complete(AsyncSummaryRequest *request);

// Check if result is ready and valid (completed and not cancelled)
bool summarize_async_is_ready(AsyncSummaryRequest *request);

// Clean up async request (call after pthread_join)
void summarize_async_cleanup(AsyncSummaryRequest *request);

#endif // SUMMARIZE_ASYNC_H

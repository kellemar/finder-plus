#include "summarize_async.h"
#include <stdlib.h>
#include <string.h>

// Worker thread function
static void *summarize_worker(void *arg)
{
    AsyncSummaryRequest *req = (AsyncSummaryRequest *)arg;

    // Initialize result
    memset(&req->result, 0, sizeof(SummaryResult));

    // Perform synchronous summarization
    SummarizeStatus status = summarize_file(req->path, &req->config, req->cache, &req->result);

    // Mark complete (thread-safe)
    pthread_mutex_lock(req->mutex);
    if (!req->cancelled) {
        req->completed = true;
        req->result.status = status;
    }
    pthread_mutex_unlock(req->mutex);

    return NULL;
}

void summarize_async_init(AsyncSummaryRequest *request, pthread_mutex_t *mutex)
{
    memset(request, 0, sizeof(AsyncSummaryRequest));
    request->mutex = mutex;
    request->completed = false;
    request->cancelled = false;
}

bool summarize_async_start(AsyncSummaryRequest *request, pthread_t *thread,
                           const char *path, SummarizeConfig *config, SummaryCache *cache)
{
    if (!request || !thread || !path || !config) {
        return false;
    }

    // Reset state
    pthread_mutex_lock(request->mutex);
    request->completed = false;
    request->cancelled = false;
    memset(&request->result, 0, sizeof(SummaryResult));
    pthread_mutex_unlock(request->mutex);

    // Copy input parameters
    strncpy(request->path, path, ASYNC_PATH_MAX - 1);
    request->path[ASYNC_PATH_MAX - 1] = '\0';
    memcpy(&request->config, config, sizeof(SummarizeConfig));
    request->cache = cache;

    // Start worker thread
    int rc = pthread_create(thread, NULL, summarize_worker, request);
    return (rc == 0);
}

void summarize_async_cancel(AsyncSummaryRequest *request)
{
    if (!request || !request->mutex) {
        return;
    }

    pthread_mutex_lock(request->mutex);
    request->cancelled = true;
    pthread_mutex_unlock(request->mutex);
}

bool summarize_async_is_complete(AsyncSummaryRequest *request)
{
    if (!request || !request->mutex) {
        return false;
    }

    pthread_mutex_lock(request->mutex);
    bool done = request->completed;
    pthread_mutex_unlock(request->mutex);

    return done;
}

bool summarize_async_is_ready(AsyncSummaryRequest *request)
{
    if (!request || !request->mutex) {
        return false;
    }

    pthread_mutex_lock(request->mutex);
    bool ready = request->completed && !request->cancelled;
    pthread_mutex_unlock(request->mutex);

    return ready;
}

void summarize_async_cleanup(AsyncSummaryRequest *request)
{
    if (!request) {
        return;
    }

    // Clear sensitive data
    memset(request->config.api_key, 0, sizeof(request->config.api_key));

    // Reset state
    request->completed = false;
    request->cancelled = false;
}

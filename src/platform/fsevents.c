#include "fsevents.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dispatch/dispatch.h>
#include <CoreServices/CoreServices.h>

#define MAX_WATCHED_PATHS 32

// FSEvents watcher internal structure
struct FSEventsWatcher {
    // Watched paths
    char paths[MAX_WATCHED_PATHS][4096];
    int path_count;

    // FSEvents stream
    FSEventStreamRef stream;
    dispatch_queue_t queue;

    // Threading
    pthread_mutex_t mutex;
    bool running;

    // Callback
    FSEventCallback callback;
    void *user_data;

    // Configuration
    double latency;
};

// FSEvents callback function
static void fsevents_callback(ConstFSEventStreamRef streamRef,
                               void *clientCallBackInfo,
                               size_t numEvents,
                               void *eventPaths,
                               const FSEventStreamEventFlags eventFlags[],
                               const FSEventStreamEventId eventIds[])
{
    (void)streamRef;

    FSEventsWatcher *watcher = (FSEventsWatcher *)clientCallBackInfo;
    if (watcher == NULL || watcher->callback == NULL) {
        return;
    }

    char **paths = (char **)eventPaths;

    for (size_t i = 0; i < numEvents; i++) {
        FSEvent event = {0};
        strncpy(event.path, paths[i], sizeof(event.path) - 1);
        event.event_id = eventIds[i];

        FSEventStreamEventFlags flags = eventFlags[i];
        bool is_dir = flags & kFSEventStreamEventFlagItemIsDir;

        // Set file/directory flags (common to all event types)
        event.flags |= is_dir ? FSEVENT_FLAG_IS_DIR : FSEVENT_FLAG_IS_FILE;
        if (flags & kFSEventStreamEventFlagItemIsSymlink) {
            event.flags |= FSEVENT_FLAG_IS_SYMLINK;
        }

        // Determine event type
        if (flags & kFSEventStreamEventFlagItemCreated) {
            event.type = is_dir ? FSEVENT_DIR_CREATED : FSEVENT_CREATED;
        } else if (flags & kFSEventStreamEventFlagItemRemoved) {
            event.type = is_dir ? FSEVENT_DIR_DELETED : FSEVENT_DELETED;
            event.flags |= FSEVENT_FLAG_ITEM_REMOVED;
        } else if (flags & kFSEventStreamEventFlagItemRenamed) {
            event.type = FSEVENT_RENAMED;
            event.flags |= FSEVENT_FLAG_ITEM_RENAMED;
        } else if (flags & kFSEventStreamEventFlagItemModified) {
            event.type = FSEVENT_MODIFIED;
        } else {
            event.type = FSEVENT_UNKNOWN;
        }

        // Call the user callback
        watcher->callback(&event, watcher->user_data);
    }
}

// Helper function to create and start the FSEvents stream
static bool create_fsevents_stream(FSEventsWatcher *watcher)
{
    // Build paths array
    CFMutableArrayRef paths_array = CFArrayCreateMutable(kCFAllocatorDefault,
                                                          (CFIndex)watcher->path_count,
                                                          &kCFTypeArrayCallBacks);

    for (int i = 0; i < watcher->path_count; i++) {
        CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault,
                                                      watcher->paths[i],
                                                      kCFStringEncodingUTF8);
        CFArrayAppendValue(paths_array, path);
        CFRelease(path);
    }

    // Create FSEvents stream context
    FSEventStreamContext context = {
        .version = 0,
        .info = watcher,
        .retain = NULL,
        .release = NULL,
        .copyDescription = NULL
    };

    // Create the stream with file-level events
    watcher->stream = FSEventStreamCreate(kCFAllocatorDefault,
                                          fsevents_callback,
                                          &context,
                                          paths_array,
                                          kFSEventStreamEventIdSinceNow,
                                          watcher->latency,
                                          kFSEventStreamCreateFlagFileEvents |
                                          kFSEventStreamCreateFlagNoDefer);

    CFRelease(paths_array);

    if (watcher->stream == NULL) {
        return false;
    }

    // Create a dispatch queue for the stream
    watcher->queue = dispatch_queue_create("com.finderplus.fsevents",
                                            DISPATCH_QUEUE_SERIAL);

    // Set the dispatch queue
    FSEventStreamSetDispatchQueue(watcher->stream, watcher->queue);

    // Start the stream
    if (!FSEventStreamStart(watcher->stream)) {
        FSEventStreamInvalidate(watcher->stream);
        FSEventStreamRelease(watcher->stream);
        dispatch_release(watcher->queue);
        watcher->stream = NULL;
        watcher->queue = NULL;
        return false;
    }

    return true;
}

FSEventsWatcher* fsevents_create(void)
{
    FSEventsWatcher *watcher = calloc(1, sizeof(FSEventsWatcher));
    if (watcher == NULL) {
        return NULL;
    }

    pthread_mutex_init(&watcher->mutex, NULL);
    watcher->running = false;
    watcher->latency = 0.5;  // Default 500ms latency

    return watcher;
}

void fsevents_destroy(FSEventsWatcher *watcher)
{
    if (watcher == NULL) {
        return;
    }

    fsevents_stop(watcher);
    pthread_mutex_destroy(&watcher->mutex);
    free(watcher);
}

bool fsevents_add_path(FSEventsWatcher *watcher, const char *path)
{
    if (watcher == NULL || path == NULL) {
        return false;
    }

    pthread_mutex_lock(&watcher->mutex);

    if (watcher->running) {
        pthread_mutex_unlock(&watcher->mutex);
        return false;  // Cannot modify while running
    }

    if (watcher->path_count >= MAX_WATCHED_PATHS) {
        pthread_mutex_unlock(&watcher->mutex);
        return false;
    }

    strncpy(watcher->paths[watcher->path_count], path,
            sizeof(watcher->paths[0]) - 1);
    watcher->path_count++;

    pthread_mutex_unlock(&watcher->mutex);
    return true;
}

bool fsevents_remove_path(FSEventsWatcher *watcher, const char *path)
{
    if (watcher == NULL || path == NULL) {
        return false;
    }

    pthread_mutex_lock(&watcher->mutex);

    if (watcher->running) {
        pthread_mutex_unlock(&watcher->mutex);
        return false;  // Cannot modify while running
    }

    for (int i = 0; i < watcher->path_count; i++) {
        if (strcmp(watcher->paths[i], path) == 0) {
            // Shift remaining paths
            for (int j = i; j < watcher->path_count - 1; j++) {
                strncpy(watcher->paths[j], watcher->paths[j + 1],
                        sizeof(watcher->paths[0]));
            }
            watcher->path_count--;
            pthread_mutex_unlock(&watcher->mutex);
            return true;
        }
    }

    pthread_mutex_unlock(&watcher->mutex);
    return false;
}

void fsevents_set_callback(FSEventsWatcher *watcher, FSEventCallback callback, void *user_data)
{
    if (watcher == NULL) {
        return;
    }

    pthread_mutex_lock(&watcher->mutex);
    watcher->callback = callback;
    watcher->user_data = user_data;
    pthread_mutex_unlock(&watcher->mutex);
}

bool fsevents_start(FSEventsWatcher *watcher)
{
    if (watcher == NULL) {
        return false;
    }

    pthread_mutex_lock(&watcher->mutex);

    if (watcher->running) {
        pthread_mutex_unlock(&watcher->mutex);
        return false;  // Already running
    }

    if (watcher->path_count == 0) {
        pthread_mutex_unlock(&watcher->mutex);
        return false;  // No paths to watch
    }

    if (!create_fsevents_stream(watcher)) {
        pthread_mutex_unlock(&watcher->mutex);
        return false;
    }

    watcher->running = true;
    pthread_mutex_unlock(&watcher->mutex);

    return true;
}

void fsevents_stop(FSEventsWatcher *watcher)
{
    if (watcher == NULL) {
        return;
    }

    pthread_mutex_lock(&watcher->mutex);

    if (!watcher->running) {
        pthread_mutex_unlock(&watcher->mutex);
        return;
    }

    watcher->running = false;

    // Stop and clean up the stream
    if (watcher->stream != NULL) {
        FSEventStreamStop(watcher->stream);
        FSEventStreamInvalidate(watcher->stream);
        FSEventStreamRelease(watcher->stream);
        watcher->stream = NULL;
    }

    // Release the dispatch queue
    if (watcher->queue != NULL) {
        dispatch_release(watcher->queue);
        watcher->queue = NULL;
    }

    pthread_mutex_unlock(&watcher->mutex);
}

bool fsevents_is_running(const FSEventsWatcher *watcher)
{
    if (watcher == NULL) {
        return false;
    }
    return watcher->running;
}

void fsevents_set_latency(FSEventsWatcher *watcher, double latency)
{
    if (watcher == NULL) {
        return;
    }

    pthread_mutex_lock(&watcher->mutex);

    if (!watcher->running) {
        watcher->latency = latency;
    }

    pthread_mutex_unlock(&watcher->mutex);
}

const char* fsevents_type_name(FSEventType type)
{
    switch (type) {
        case FSEVENT_CREATED:     return "created";
        case FSEVENT_MODIFIED:    return "modified";
        case FSEVENT_DELETED:     return "deleted";
        case FSEVENT_RENAMED:     return "renamed";
        case FSEVENT_DIR_CREATED: return "dir_created";
        case FSEVENT_DIR_DELETED: return "dir_deleted";
        default:                  return "unknown";
    }
}

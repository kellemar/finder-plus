#ifndef FSEVENTS_H
#define FSEVENTS_H

#include <stdbool.h>
#include <stdint.h>

// FSEvents watcher for macOS file system change notifications

// Event types
typedef enum FSEventType {
    FSEVENT_CREATED = 0,
    FSEVENT_MODIFIED,
    FSEVENT_DELETED,
    FSEVENT_RENAMED,
    FSEVENT_DIR_CREATED,
    FSEVENT_DIR_DELETED,
    FSEVENT_UNKNOWN
} FSEventType;

// Event flags for additional context
typedef enum FSEventFlags {
    FSEVENT_FLAG_NONE         = 0,
    FSEVENT_FLAG_IS_DIR       = (1 << 0),
    FSEVENT_FLAG_IS_FILE      = (1 << 1),
    FSEVENT_FLAG_IS_SYMLINK   = (1 << 2),
    FSEVENT_FLAG_ITEM_RENAMED = (1 << 3),
    FSEVENT_FLAG_ITEM_REMOVED = (1 << 4)
} FSEventFlags;

// Event structure
typedef struct FSEvent {
    char path[4096];
    FSEventType type;
    uint32_t flags;
    uint64_t event_id;
} FSEvent;

// Callback for file system events
typedef void (*FSEventCallback)(const FSEvent *event, void *user_data);

// FSEvents watcher context (opaque)
typedef struct FSEventsWatcher FSEventsWatcher;

// Create a new FSEvents watcher
FSEventsWatcher* fsevents_create(void);

// Destroy the watcher and free resources
void fsevents_destroy(FSEventsWatcher *watcher);

// Add a directory to watch (can be called multiple times)
bool fsevents_add_path(FSEventsWatcher *watcher, const char *path);

// Remove a watched directory
bool fsevents_remove_path(FSEventsWatcher *watcher, const char *path);

// Set callback for events
void fsevents_set_callback(FSEventsWatcher *watcher, FSEventCallback callback, void *user_data);

// Start watching (non-blocking, uses a dedicated thread)
bool fsevents_start(FSEventsWatcher *watcher);

// Stop watching
void fsevents_stop(FSEventsWatcher *watcher);

// Check if the watcher is running
bool fsevents_is_running(const FSEventsWatcher *watcher);

// Set latency (seconds) - how long to batch events before callback
void fsevents_set_latency(FSEventsWatcher *watcher, double latency);

// Get event type name
const char* fsevents_type_name(FSEventType type);

#endif // FSEVENTS_H

#ifndef PLATFORM_TRASH_H
#define PLATFORM_TRASH_H

#include <stdbool.h>

// Move a file or directory to the system Trash
// Uses native macOS NSFileManager API to safely move items to Trash
// Returns true on success, false on failure
bool platform_move_to_trash(const char *path);

#endif // PLATFORM_TRASH_H

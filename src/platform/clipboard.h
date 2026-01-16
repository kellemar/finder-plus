#ifndef PLATFORM_CLIPBOARD_H
#define PLATFORM_CLIPBOARD_H

#include <stdbool.h>

// Platform clipboard integration for cross-app copy/paste

// Initialize platform clipboard (call once at startup)
void platform_clipboard_init(void);

// Copy file paths to system clipboard (for cross-app paste)
// Returns true on success
bool platform_clipboard_copy_files(const char **paths, int count);

// Check if system clipboard has files
bool platform_clipboard_has_files(void);

// Get file count from system clipboard
int platform_clipboard_get_file_count(void);

// Get file path at index from system clipboard
// Returns pointer to static buffer (overwritten on next call)
// Returns NULL if index out of range or no files
const char* platform_clipboard_get_file_path(int index);

// Copy text to system clipboard
bool platform_clipboard_copy_text(const char *text);

// Get text from system clipboard
// Returns pointer to static buffer (overwritten on next call)
// Returns NULL if no text
const char* platform_clipboard_get_text(void);

// Clear the system clipboard
void platform_clipboard_clear(void);

#endif // PLATFORM_CLIPBOARD_H

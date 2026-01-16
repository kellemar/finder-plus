#ifndef FILE_VIEW_MODAL_H
#define FILE_VIEW_MODAL_H

#include <stdbool.h>

// Maximum file path length
#define FILE_VIEW_PATH_MAX 4096

// File view modal types
typedef enum FileViewType {
    FILE_VIEW_NONE,
    FILE_VIEW_TEXT,
    FILE_VIEW_IMAGE
} FileViewType;

// File view modal state
typedef struct FileViewModalState {
    bool visible;                       // Whether modal is visible
    FileViewType type;                  // Type of content being displayed

    // File info
    char file_path[FILE_VIEW_PATH_MAX]; // Full path to file
    char filename[256];                 // Extracted filename for display

    // Text content
    char *text_content;                 // Dynamically allocated text content
    int total_lines;                    // Total number of lines
    int scroll_offset;                  // Current scroll position (line number)
    int visible_lines;                  // Number of visible lines (set during draw)

    // Image content
    unsigned int texture_id;            // Raylib texture ID (0 if none)
    int image_width;                    // Original image width
    int image_height;                   // Original image height
} FileViewModalState;

// Forward declaration
struct App;

// Initialize file view modal state
void file_view_modal_init(FileViewModalState *modal);

// Free resources used by the modal
void file_view_modal_free(FileViewModalState *modal);

// Show modal with text content
void file_view_modal_show_text(FileViewModalState *modal, const char *file_path,
                               const char *content);

// Show modal with image (texture_id is an existing Raylib texture)
void file_view_modal_show_image(FileViewModalState *modal, const char *file_path,
                                unsigned int texture_id, int width, int height);

// Hide the modal
void file_view_modal_hide(FileViewModalState *modal);

// Check if modal is visible
bool file_view_modal_is_visible(FileViewModalState *modal);

// Scroll text content
void file_view_modal_scroll_down(FileViewModalState *modal, int lines);
void file_view_modal_scroll_up(FileViewModalState *modal, int lines);
void file_view_modal_scroll_to_top(FileViewModalState *modal);
void file_view_modal_scroll_to_bottom(FileViewModalState *modal);

// Get filename from path
const char* file_view_modal_get_filename(FileViewModalState *modal);

// Handle input (called from app_handle_input when modal is visible)
// Returns true if input was consumed
bool file_view_modal_handle_input(struct App *app);

// Draw the modal (called from app_draw when modal is visible)
void file_view_modal_draw(struct App *app);

#endif // FILE_VIEW_MODAL_H

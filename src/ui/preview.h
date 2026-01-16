#ifndef PREVIEW_H
#define PREVIEW_H

#include <stdbool.h>
#include <sys/types.h>
#include <pthread.h>
#include "progress_indicator.h"

#define PREVIEW_DEFAULT_WIDTH 300
#define PREVIEW_MIN_WIDTH 200
#define PREVIEW_MAX_WIDTH 500
#define PREVIEW_EDIT_BUFFER_SIZE 512

// Summary pane constants
#define SUMMARY_PANE_HEIGHT 150      // Default height of summary pane
#define SUMMARY_PANE_MIN_HEIGHT 100  // Minimum height
#define SUMMARY_PANE_MAX_HEIGHT 250  // Maximum height

// Preview file types
typedef enum PreviewType {
    PREVIEW_NONE,
    PREVIEW_TEXT,
    PREVIEW_IMAGE,
    PREVIEW_CODE,
    PREVIEW_MARKDOWN,
    PREVIEW_PDF,
    PREVIEW_VIDEO,
    PREVIEW_UNKNOWN
} PreviewType;

// Image edit state
typedef enum ImageEditState {
    IMAGE_EDIT_NONE,       // Not editing
    IMAGE_EDIT_INPUT,      // Showing text input for edit prompt
    IMAGE_EDIT_LOADING,    // Sending to Gemini API
    IMAGE_EDIT_SUCCESS,    // Edit completed
    IMAGE_EDIT_ERROR       // Edit failed
} ImageEditState;

// Preview state
typedef struct PreviewState {
    bool visible;               // Whether preview panel is visible
    int width;                  // Panel width
    bool resizing;              // Whether resizing the panel

    // Cached preview content
    char file_path[4096];       // Currently previewed file
    PreviewType type;           // Type of preview

    // Text preview
    char *text_content;         // Loaded text content (raw)
    int text_lines;             // Number of lines (raw)
    int scroll_offset;          // Scroll position (in wrapped lines)

    // Wrapped text for PREVIEW_TEXT (prose display)
    char *wrapped_content;      // Word-truncated content (~300 words + "...")
    int wrapped_total_lines;    // Total wrapped lines for scrollbar

    // Image preview
    unsigned int texture_id;    // Raylib texture ID (0 if none)
    int image_width;            // Original image width
    int image_height;           // Original image height
    bool image_loaded;          // Whether image is loaded
    char image_format[16];      // Image format (PNG, JPEG, etc.)
    int image_bit_depth;        // Bit depth of loaded image

    // Image editing (AI-powered)
    ImageEditState edit_state;          // Current edit state
    char edit_buffer[PREVIEW_EDIT_BUFFER_SIZE];  // Edit prompt input buffer
    int edit_cursor;                    // Cursor position in edit buffer
    char edit_error[256];               // Error message if edit failed
    char edit_result_path[4096];        // Path to edited image
    ProgressIndicator edit_progress;    // Progress indicator for AI edit

    // Video preview
    bool video_loaded;                  // Whether video thumbnail is loaded
    bool video_playing;                 // Whether video is currently playing
    char video_thumbnail_path[4096];    // Path to cached thumbnail
    unsigned int video_thumbnail_id;    // Thumbnail texture ID
    int video_width;                    // Video width
    int video_height;                   // Video height
    float video_duration;               // Video duration in seconds
    pid_t ffplay_pid;                   // ffplay process ID (0 if not running)
    bool video_playback_active;         // Whether playback is active
    char video_format[16];              // Video codec format (H.264, HEVC, etc.)
    int video_bit_depth;                // Video bit depth (8, 10, 12)

    // In-pane video playback
    unsigned int video_frame_texture_id;  // Texture for current video frame
    unsigned char *video_frame_buffer;    // Raw RGB frame buffer (for texture update)
    unsigned char *video_decode_buffer;   // Decode buffer (written by thread)
    int video_frame_width;                // Frame width
    int video_frame_height;               // Frame height
    pid_t video_decoder_pid;              // ffmpeg decoder process
    int video_pipe_fd;                    // Pipe for frames
    bool video_frame_ready;               // New frame available from thread
    bool video_paused;                    // Playback paused
    float video_fps;                      // Video frame rate
    bool video_inpane_active;             // In-pane playback active
    double video_last_frame_time;         // Time of last frame update for FPS limiting

    // Video decode thread
    pthread_t video_thread;               // Background decode thread
    pthread_mutex_t video_mutex;          // Protects frame buffer
    bool video_thread_running;            // Thread should keep running
    bool video_thread_active;             // Thread is currently active
    bool video_eof;                       // Video reached end

    // Summary pane state
    bool summary_pane_visible;            // Whether summary pane is expanded
    int summary_pane_height;              // Current height of summary pane
    int summary_scroll_offset;            // Scroll position in summary pane
    int summary_total_lines;              // Total wrapped lines in summary
} PreviewState;

// Forward declaration
struct App;

// Initialize preview state
void preview_init(PreviewState *preview);

// Free preview resources
void preview_free(PreviewState *preview);

// Toggle preview panel visibility
void preview_toggle(PreviewState *preview);

// Load preview for a file
void preview_load(PreviewState *preview, const char *file_path);

// Clear current preview
void preview_clear(PreviewState *preview);

// Determine preview type from file extension
PreviewType preview_type_from_extension(const char *extension);

// Handle preview input (resize, scroll)
// Returns true if input was consumed (blocks other input handlers)
bool preview_handle_input(struct App *app);

// Draw preview panel
void preview_draw(struct App *app);

// Get preview width (0 if not visible)
int preview_get_width(PreviewState *preview);

// Check if preview is visible
bool preview_is_visible(PreviewState *preview);

#endif // PREVIEW_H

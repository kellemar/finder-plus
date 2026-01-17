#include "preview.h"
#include "video.h"
#include "tabs.h"
#include "sidebar.h"
#include "browser.h"
#include "../app.h"
#include "../core/filesystem.h"
#include "../core/search.h"
#include "../ai/summarize.h"
#include "../utils/theme.h"
#include "../utils/text.h"
#include "../utils/font.h"
#include "../api/gemini_client.h"
#include "../api/auth.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <stdatomic.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Maximum text preview size (1MB)
#define MAX_TEXT_SIZE (1024 * 1024)

// Maximum words to show in PREVIEW_TEXT (prose view)
#define MAX_PREVIEW_WORDS 300

// Maximum lines to display
#define MAX_PREVIEW_LINES 1000

// Helper: Get bit depth from Raylib pixel format
static int preview_get_bit_depth_from_format(int format)
{
    switch (format) {
        case PIXELFORMAT_UNCOMPRESSED_GRAYSCALE: return 8;
        case PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA: return 16;
        case PIXELFORMAT_UNCOMPRESSED_R5G6B5: return 16;
        case PIXELFORMAT_UNCOMPRESSED_R8G8B8: return 24;
        case PIXELFORMAT_UNCOMPRESSED_R5G5B5A1: return 16;
        case PIXELFORMAT_UNCOMPRESSED_R4G4B4A4: return 16;
        case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8: return 32;
        case PIXELFORMAT_UNCOMPRESSED_R32: return 32;
        case PIXELFORMAT_UNCOMPRESSED_R32G32B32: return 96;
        case PIXELFORMAT_UNCOMPRESSED_R32G32B32A32: return 128;
        case PIXELFORMAT_UNCOMPRESSED_R16: return 16;
        case PIXELFORMAT_UNCOMPRESSED_R16G16B16: return 48;
        case PIXELFORMAT_UNCOMPRESSED_R16G16B16A16: return 64;
        default: return 0;  // Unknown/compressed
    }
}

// Helper: Extract format string from file extension
static void preview_extract_format_from_extension(const char *ext, char *format_out, size_t format_size)
{
    if (!ext || !format_out || format_size == 0) return;

    // Convert to uppercase for display
    size_t len = strlen(ext);
    if (len >= format_size) len = format_size - 1;

    for (size_t i = 0; i < len; i++) {
        format_out[i] = toupper((unsigned char)ext[i]);
    }
    format_out[len] = '\0';
}

// Video decode thread function - continuously reads frames from ffmpeg
static void *video_decode_thread(void *arg)
{
    PreviewState *preview = (PreviewState *)arg;
    size_t frame_size = preview->video_frame_width * preview->video_frame_height * 3;
    size_t bytes_read = 0;

    while (preview->video_thread_running) {
        // Wait while paused
        while (preview->video_paused && preview->video_thread_running) {
            usleep(50000);  // Check every 50ms
        }
        if (!preview->video_thread_running) break;

        // Read a complete frame (blocking read in thread is fine)
        while (bytes_read < frame_size && preview->video_thread_running) {
            ssize_t n = read(preview->video_pipe_fd,
                           preview->video_decode_buffer + bytes_read,
                           frame_size - bytes_read);
            if (n > 0) {
                bytes_read += n;
            } else if (n == 0) {
                // EOF - video ended
                preview->video_eof = true;
                return NULL;
            } else {
                // Error (but not EAGAIN since we're blocking)
                if (errno != EINTR) {
                    preview->video_eof = true;
                    return NULL;
                }
            }
        }

        if (bytes_read == frame_size) {
            // Got a complete frame - copy to display buffer
            pthread_mutex_lock(&preview->video_mutex);
            memcpy(preview->video_frame_buffer, preview->video_decode_buffer, frame_size);
            preview->video_frame_ready = true;
            pthread_mutex_unlock(&preview->video_mutex);
            bytes_read = 0;

            // Sleep to match video FPS (prevents reading too far ahead)
            if (preview->video_fps > 0) {
                usleep((useconds_t)(1000000.0 / preview->video_fps));
            } else {
                usleep(33333);  // ~30 FPS default
            }
        }
    }
    return NULL;
}

// Helper: Stop in-pane video playback and cleanup resources
static void preview_stop_video_playback(PreviewState *preview)
{
    if (preview->video_inpane_active) {
        // Signal thread to stop and wait for it
        if (preview->video_thread_active) {
            preview->video_thread_running = false;
            pthread_join(preview->video_thread, NULL);
            preview->video_thread_active = false;
            pthread_mutex_destroy(&preview->video_mutex);
        }

        video_stop_inpane_playback(preview->video_decoder_pid, preview->video_pipe_fd);

        if (preview->video_frame_texture_id != 0) {
            Texture2D tex = {
                .id = preview->video_frame_texture_id,
                .width = preview->video_frame_width,
                .height = preview->video_frame_height
            };
            UnloadTexture(tex);
            preview->video_frame_texture_id = 0;
        }

        if (preview->video_frame_buffer) {
            free(preview->video_frame_buffer);
            preview->video_frame_buffer = NULL;
        }
        if (preview->video_decode_buffer) {
            free(preview->video_decode_buffer);
            preview->video_decode_buffer = NULL;
        }

        preview->video_inpane_active = false;
        preview->video_paused = false;
        preview->video_decoder_pid = 0;
        preview->video_pipe_fd = -1;
        preview->video_frame_width = 0;
        preview->video_frame_height = 0;
        preview->video_frame_ready = false;
        preview->video_eof = false;
    }
}

// Helper: Start in-pane video playback with default settings
static bool preview_start_video_playback(PreviewState *preview)
{
    int frame_width = preview->video_width > 0 ? preview->video_width : 640;
    int frame_height = preview->video_height > 0 ? preview->video_height : 480;

    // Scale down for performance
    if (frame_width > 480) {
        float s = 480.0f / frame_width;
        frame_width = 480;
        frame_height = (int)(frame_height * s);
    }

    preview->video_pipe_fd = video_start_inpane_playback(
        preview->file_path, frame_width, frame_height,
        &preview->video_decoder_pid, &preview->video_fps);

    if (preview->video_pipe_fd < 0) {
        return false;
    }

    // Keep pipe blocking for the thread
    preview->video_frame_width = frame_width;
    preview->video_frame_height = frame_height;
    size_t buffer_size = (size_t)frame_width * (size_t)frame_height * 3;
    preview->video_frame_buffer = malloc(buffer_size);
    if (!preview->video_frame_buffer) {
        video_stop_inpane_playback(preview->video_decoder_pid, preview->video_pipe_fd);
        preview->video_pipe_fd = -1;
        preview->video_decoder_pid = 0;
        return false;
    }
    preview->video_decode_buffer = malloc(buffer_size);
    if (!preview->video_decode_buffer) {
        free(preview->video_frame_buffer);
        preview->video_frame_buffer = NULL;
        video_stop_inpane_playback(preview->video_decoder_pid, preview->video_pipe_fd);
        preview->video_pipe_fd = -1;
        preview->video_decoder_pid = 0;
        return false;
    }
    preview->video_inpane_active = true;
    preview->video_paused = false;
    preview->video_frame_ready = false;
    preview->video_eof = false;
    preview->video_last_frame_time = GetTime();

    // Create texture for video frames
    Image blank = GenImageColor(frame_width, frame_height, BLACK);
    Texture2D tex = LoadTextureFromImage(blank);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    preview->video_frame_texture_id = tex.id;
    UnloadImage(blank);

    // Start decode thread
    pthread_mutex_init(&preview->video_mutex, NULL);
    preview->video_thread_running = true;
    if (pthread_create(&preview->video_thread, NULL, video_decode_thread, preview) == 0) {
        preview->video_thread_active = true;
    } else {
        // Thread creation failed, cleanup
        preview->video_thread_running = false;
        pthread_mutex_destroy(&preview->video_mutex);
        preview_stop_video_playback(preview);
        return false;
    }

    return true;
}

// Helper: Calculate scale factor to fit content within bounds
static float calculate_preview_scale(int src_width, int src_height, int max_width, int max_height)
{
    float scale = 1.0f;
    if (src_width > max_width) {
        scale = (float)max_width / src_width;
    }
    if (src_height * scale > max_height) {
        scale = (float)max_height / src_height;
    }
    return scale;
}

// Helper: Draw a button and return if it was clicked
typedef struct {
    bool hovered;
    bool clicked;
} ButtonResult;

static ButtonResult draw_button(int x, int y, int width, int height,
                                const char *text, Color normal_color, Color hover_color)
{
    ButtonResult result = {0};
    Rectangle rect = {(float)x, (float)y, (float)width, (float)height};
    result.hovered = CheckCollisionPointRec(GetMousePosition(), rect);
    result.clicked = result.hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    DrawRectangleRec(rect, result.hovered ? hover_color : normal_color);
    int text_w = MeasureTextCustom(text, FONT_SIZE_SMALL);
    DrawTextCustom(text, x + (width - text_w) / 2,
                   y + (height - FONT_SIZE_SMALL) / 2,
                   FONT_SIZE_SMALL, g_theme.background);

    return result;
}

void preview_init(PreviewState *preview)
{
    memset(preview, 0, sizeof(PreviewState));
    preview->visible = false;
    preview->width = PREVIEW_DEFAULT_WIDTH;
    preview->resizing = false;
    preview->file_path[0] = '\0';
    preview->type = PREVIEW_NONE;
    preview->text_content = NULL;
    preview->text_lines = 0;
    preview->scroll_offset = 0;
    preview->wrapped_content = NULL;
    preview->wrapped_total_lines = 0;
    preview->texture_id = 0;
    preview->image_width = 0;
    preview->image_height = 0;
    preview->image_loaded = false;

    // Image editing state
    preview->edit_state = IMAGE_EDIT_NONE;
    preview->edit_buffer[0] = '\0';
    preview->edit_cursor = 0;
    preview->edit_error[0] = '\0';
    preview->edit_result_path[0] = '\0';
    progress_indicator_init(&preview->edit_progress, PROGRESS_SPINNER);
    progress_indicator_set_message(&preview->edit_progress, "Editing image with AI...");

    // Video state
    preview->video_loaded = false;
    preview->video_playing = false;
    preview->video_thumbnail_path[0] = '\0';
    preview->video_thumbnail_id = 0;
    preview->video_width = 0;
    preview->video_height = 0;
    preview->video_duration = 0;
    preview->ffplay_pid = 0;
    preview->video_playback_active = false;
    preview->video_pipe_fd = -1;  // -1 indicates not open

    // Summary pane state
    preview->summary_pane_visible = false;
    preview->summary_pane_height = SUMMARY_PANE_HEIGHT;
    preview->summary_scroll_offset = 0;
    preview->summary_total_lines = 0;
}

void preview_free(PreviewState *preview)
{
    preview_clear(preview);
}

void preview_toggle(PreviewState *preview)
{
    preview->visible = !preview->visible;
    if (!preview->visible) {
        preview_clear(preview);
    }
}

void preview_clear(PreviewState *preview)
{
    if (preview->text_content) {
        free(preview->text_content);
        preview->text_content = NULL;
    }
    if (preview->wrapped_content) {
        free(preview->wrapped_content);
        preview->wrapped_content = NULL;
    }
    preview->text_lines = 0;
    preview->wrapped_total_lines = 0;
    preview->scroll_offset = 0;

    if (preview->image_loaded && preview->texture_id != 0) {
        Texture2D tex = { .id = preview->texture_id, .width = preview->image_width, .height = preview->image_height };
        UnloadTexture(tex);
        preview->texture_id = 0;
        preview->image_loaded = false;
    }
    preview->image_format[0] = '\0';
    preview->image_bit_depth = 0;

    // Clear image edit state
    preview->edit_state = IMAGE_EDIT_NONE;
    preview->edit_buffer[0] = '\0';
    preview->edit_cursor = 0;
    preview->edit_error[0] = '\0';
    preview->edit_result_path[0] = '\0';

    // Clear video state
    // Stop in-pane video playback if active
    preview_stop_video_playback(preview);

    // Stop external ffplay if running (legacy)
    if (preview->video_playback_active && preview->ffplay_pid > 0) {
        video_stop_playback(preview->ffplay_pid);
    }
    if (preview->video_loaded && preview->video_thumbnail_id != 0) {
        Texture2D tex = { .id = preview->video_thumbnail_id, .width = preview->image_width, .height = preview->image_height };
        UnloadTexture(tex);
        preview->video_thumbnail_id = 0;
    }
    preview->video_loaded = false;
    preview->video_playing = false;
    preview->video_thumbnail_path[0] = '\0';
    preview->video_width = 0;
    preview->video_height = 0;
    preview->video_duration = 0;
    preview->ffplay_pid = 0;
    preview->video_playback_active = false;
    preview->video_format[0] = '\0';
    preview->video_bit_depth = 0;

    preview->file_path[0] = '\0';
    preview->type = PREVIEW_NONE;
}

PreviewType preview_type_from_extension(const char *extension)
{
    if (!extension || extension[0] == '\0') {
        return PREVIEW_UNKNOWN;
    }

    // Convert to lowercase for comparison
    char ext_lower[32];
    strncpy(ext_lower, extension, sizeof(ext_lower) - 1);
    ext_lower[sizeof(ext_lower) - 1] = '\0';
    for (int i = 0; ext_lower[i]; i++) {
        ext_lower[i] = tolower((unsigned char)ext_lower[i]);
    }

    // Images
    if (strcmp(ext_lower, "png") == 0 ||
        strcmp(ext_lower, "jpg") == 0 ||
        strcmp(ext_lower, "jpeg") == 0 ||
        strcmp(ext_lower, "gif") == 0 ||
        strcmp(ext_lower, "bmp") == 0) {
        return PREVIEW_IMAGE;
    }

    // Code files
    if (strcmp(ext_lower, "c") == 0 ||
        strcmp(ext_lower, "h") == 0 ||
        strcmp(ext_lower, "cpp") == 0 ||
        strcmp(ext_lower, "hpp") == 0 ||
        strcmp(ext_lower, "py") == 0 ||
        strcmp(ext_lower, "js") == 0 ||
        strcmp(ext_lower, "ts") == 0 ||
        strcmp(ext_lower, "go") == 0 ||
        strcmp(ext_lower, "rs") == 0 ||
        strcmp(ext_lower, "java") == 0 ||
        strcmp(ext_lower, "swift") == 0 ||
        strcmp(ext_lower, "css") == 0 ||
        strcmp(ext_lower, "html") == 0 ||
        strcmp(ext_lower, "json") == 0 ||
        strcmp(ext_lower, "yaml") == 0 ||
        strcmp(ext_lower, "yml") == 0 ||
        strcmp(ext_lower, "toml") == 0 ||
        strcmp(ext_lower, "xml") == 0 ||
        strcmp(ext_lower, "sh") == 0) {
        return PREVIEW_CODE;
    }

    // Markdown
    if (strcmp(ext_lower, "md") == 0 ||
        strcmp(ext_lower, "markdown") == 0) {
        return PREVIEW_MARKDOWN;
    }

    // PDF
    if (strcmp(ext_lower, "pdf") == 0) {
        return PREVIEW_PDF;
    }

    // Video files
    if (strcmp(ext_lower, "mp4") == 0 ||
        strcmp(ext_lower, "mov") == 0 ||
        strcmp(ext_lower, "mkv") == 0 ||
        strcmp(ext_lower, "avi") == 0 ||
        strcmp(ext_lower, "webm") == 0 ||
        strcmp(ext_lower, "m4v") == 0) {
        return PREVIEW_VIDEO;
    }

    // Plain text
    if (strcmp(ext_lower, "txt") == 0 ||
        strcmp(ext_lower, "log") == 0 ||
        strcmp(ext_lower, "cfg") == 0 ||
        strcmp(ext_lower, "conf") == 0 ||
        strcmp(ext_lower, "ini") == 0) {
        return PREVIEW_TEXT;
    }

    return PREVIEW_UNKNOWN;
}

static int count_lines(const char *text)
{
    int count = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') count++;
    }
    return count;
}

void preview_load(PreviewState *preview, const char *file_path)
{
    // Skip if same file
    if (strcmp(preview->file_path, file_path) == 0 && preview->type != PREVIEW_NONE) {
        return;
    }

    preview_clear(preview);

    strncpy(preview->file_path, file_path, sizeof(preview->file_path) - 1);
    preview->file_path[sizeof(preview->file_path) - 1] = '\0';

    // Get extension
    const char *ext = strrchr(file_path, '.');
    if (ext) ext++;
    else ext = "";

    preview->type = preview_type_from_extension(ext);

    switch (preview->type) {
        case PREVIEW_IMAGE: {
            // Load image with raylib - use LoadImage to get format info
            Image img = LoadImage(file_path);
            if (img.data != NULL) {
                Texture2D tex = LoadTextureFromImage(img);
                if (tex.id != 0) {
                    // Apply bilinear filtering for smooth scaling
                    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
                    preview->texture_id = tex.id;
                    preview->image_width = tex.width;
                    preview->image_height = tex.height;
                    preview->image_loaded = true;
                    preview->image_bit_depth = preview_get_bit_depth_from_format(img.format);
                    preview_extract_format_from_extension(ext, preview->image_format, sizeof(preview->image_format));
                } else {
                    preview->type = PREVIEW_UNKNOWN;
                }
                UnloadImage(img);
            } else {
                preview->type = PREVIEW_UNKNOWN;
            }
            break;
        }

        case PREVIEW_TEXT:
        case PREVIEW_CODE:
        case PREVIEW_MARKDOWN: {
            // Load text file
            FILE *f = fopen(file_path, "r");
            if (f) {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fseek(f, 0, SEEK_SET);

                if (size > MAX_TEXT_SIZE) {
                    size = MAX_TEXT_SIZE;
                }

                preview->text_content = malloc(size + 1);
                if (preview->text_content) {
                    size_t read = fread(preview->text_content, 1, size, f);
                    preview->text_content[read] = '\0';
                    preview->text_lines = count_lines(preview->text_content);

                    // For PREVIEW_TEXT, create wrapped content (prose view)
                    if (preview->type == PREVIEW_TEXT) {
                        preview->wrapped_content = truncate_at_words(preview->text_content, MAX_PREVIEW_WORDS);
                        if (preview->wrapped_content) {
                            // Calculate wrapped line count for scrollbar
                            int content_width = preview->width - PADDING * 2;
                            preview->wrapped_total_lines = measure_text_lines(preview->wrapped_content, content_width, FONT_SIZE_SMALL);
                        }
                    }
                }
                fclose(f);
            }
            break;
        }

        case PREVIEW_PDF:
            // PDF preview would require additional library (e.g., poppler)
            // For now, show file info
            break;

        case PREVIEW_VIDEO: {
            // Generate or load cached thumbnail
            char thumb_path[4096];
            if (video_generate_thumbnail(file_path, thumb_path, sizeof(thumb_path))) {
                Texture2D tex = LoadTexture(thumb_path);
                if (tex.id != 0) {
                    // Apply bilinear filtering for smooth scaling
                    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
                    preview->video_thumbnail_id = tex.id;
                    preview->image_width = tex.width;
                    preview->image_height = tex.height;
                    preview->video_loaded = true;
                    strncpy(preview->video_thumbnail_path, thumb_path, sizeof(preview->video_thumbnail_path) - 1);
                }
            }
            // Get video metadata (basic)
            video_get_metadata(file_path, &preview->video_duration,
                               &preview->video_width, &preview->video_height);
            // Get extended video metadata (codec, bit depth)
            video_get_extended_metadata(file_path, preview->video_format, sizeof(preview->video_format),
                                        &preview->video_bit_depth);
            break;
        }

        case PREVIEW_NONE:
        case PREVIEW_UNKNOWN:
            break;
    }
}

int preview_get_width(PreviewState *preview)
{
    return preview->visible ? preview->width : 0;
}

bool preview_is_visible(PreviewState *preview)
{
    return preview->visible;
}

bool preview_handle_input(struct App *app)
{
    PreviewState *preview = &app->preview;

    // Handle image edit input mode - MUST block all other input
    if (preview->visible && preview->type == PREVIEW_IMAGE &&
        preview->edit_state == IMAGE_EDIT_INPUT) {

        // Handle escape to cancel
        if (IsKeyPressed(KEY_ESCAPE)) {
            preview->edit_state = IMAGE_EDIT_NONE;
            return true;
        }

        // Handle enter to submit
        if (IsKeyPressed(KEY_ENTER) && strlen(preview->edit_buffer) > 0) {
            preview->edit_state = IMAGE_EDIT_LOADING;
            return true;
        }

        // Handle backspace
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            if (preview->edit_cursor > 0) {
                // Remove character before cursor
                int len = (int)strlen(preview->edit_buffer);
                for (int i = preview->edit_cursor - 1; i < len; i++) {
                    preview->edit_buffer[i] = preview->edit_buffer[i + 1];
                }
                preview->edit_cursor--;
            }
            return true;
        }

        // Handle delete key
        if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) {
            int len = (int)strlen(preview->edit_buffer);
            if (preview->edit_cursor < len) {
                // Remove character at cursor
                for (int i = preview->edit_cursor; i < len; i++) {
                    preview->edit_buffer[i] = preview->edit_buffer[i + 1];
                }
            }
            return true;
        }

        // Handle left/right arrow keys for cursor movement
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
            if (preview->edit_cursor > 0) {
                preview->edit_cursor--;
            }
            return true;
        }
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
            int len = (int)strlen(preview->edit_buffer);
            if (preview->edit_cursor < len) {
                preview->edit_cursor++;
            }
            return true;
        }

        // Handle character input
        int key = GetCharPressed();
        while (key > 0) {
            if (key >= 32 && key <= 126) {  // Printable ASCII
                int len = (int)strlen(preview->edit_buffer);
                if (len < PREVIEW_EDIT_BUFFER_SIZE - 1) {
                    // Insert at cursor
                    for (int i = len; i >= preview->edit_cursor; i--) {
                        preview->edit_buffer[i + 1] = preview->edit_buffer[i];
                    }
                    preview->edit_buffer[preview->edit_cursor] = (char)key;
                    preview->edit_cursor++;
                }
            }
            key = GetCharPressed();
        }

        // Always consume input while in edit mode
        return true;
    }

    // Toggle preview: Space
    if (IsKeyPressed(KEY_SPACE)) {
        preview_toggle(preview);

        // Load preview for selected file if now visible
        if (preview->visible && app->directory.count > 0) {
            FileEntry *entry = &app->directory.entries[app->selected_index];
            if (!entry->is_directory) {
                preview_load(preview, entry->path);
            }
        }
        return false;
    }

    if (!preview->visible) return false;

    // Get mouse position and check if over preview pane
    Vector2 mouse = GetMousePosition();
    int preview_x = app->width - preview->width;
    bool mouse_over_preview = (mouse.x >= preview_x);

    // Scroll preview content with arrow keys (supports holding for continuous scroll)
    // Only when mouse is over preview pane

    if (mouse_over_preview && (preview->type == PREVIEW_TEXT || preview->type == PREVIEW_CODE || preview->type == PREVIEW_MARKDOWN)) {
        // Calculate max scroll based on content type
        int max_scroll;
        if (preview->type == PREVIEW_TEXT && preview->wrapped_total_lines > 0) {
            max_scroll = preview->wrapped_total_lines - 1;
        } else {
            max_scroll = preview->text_lines - 1;
        }
        if (max_scroll < 0) max_scroll = 0;

        // Down arrow - scroll down
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
            if (preview->scroll_offset < max_scroll) {
                preview->scroll_offset++;
            }
        }
        // Up arrow - scroll up
        if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
            if (preview->scroll_offset > 0) {
                preview->scroll_offset--;
            }
        }
    }

    // Mouse wheel scroll in preview (reuse mouse and preview_x from above)
    if (mouse_over_preview) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            // Check if mouse is over summary pane
            int tab_height = tabs_get_height(&app->tabs);
            int search_height = search_is_active(&app->search) ? 32 : 0;
            int content_offset = tab_height + search_height;
            int total_preview_height = app->height - STATUSBAR_HEIGHT - content_offset;
            int summary_pane_height = preview->summary_pane_visible ? preview->summary_pane_height : 0;
            int summary_pane_y = content_offset + total_preview_height - summary_pane_height;

            bool mouse_over_summary = preview->summary_pane_visible &&
                                      mouse.y >= summary_pane_y;

            if (mouse_over_summary && preview->summary_total_lines > 0) {
                // Scroll summary pane
                int line_height = FONT_SIZE_SMALL + 2;
                int available_height = summary_pane_height - ROW_HEIGHT - PADDING * 3;
                int visible_lines = available_height / line_height;
                if (visible_lines < 1) visible_lines = 1;

                int max_scroll = preview->summary_total_lines - visible_lines;
                if (max_scroll < 0) max_scroll = 0;

                preview->summary_scroll_offset -= (int)(wheel * 3);
                if (preview->summary_scroll_offset < 0) preview->summary_scroll_offset = 0;
                if (preview->summary_scroll_offset > max_scroll) {
                    preview->summary_scroll_offset = max_scroll;
                }
            } else if (preview->type == PREVIEW_TEXT || preview->type == PREVIEW_CODE || preview->type == PREVIEW_MARKDOWN) {
                // Scroll main content
                int max_scroll;
                if (preview->type == PREVIEW_TEXT && preview->wrapped_total_lines > 0) {
                    max_scroll = preview->wrapped_total_lines - 1;
                } else {
                    max_scroll = preview->text_lines - 1;
                }
                if (max_scroll < 0) max_scroll = 0;

                preview->scroll_offset -= (int)(wheel * 3);
                if (preview->scroll_offset < 0) preview->scroll_offset = 0;
                if (preview->scroll_offset > max_scroll) {
                    preview->scroll_offset = max_scroll;
                }
            }
        }
    }

    // Video in-pane playback: control button handling
    if (preview->type == PREVIEW_VIDEO && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int tab_height = tabs_get_height(&app->tabs);
        int search_height = search_is_active(&app->search) ? 32 : 0;
        int content_offset = tab_height + search_height;
        int content_x = preview_x + PADDING;
        int content_y = content_offset + PADDING;
        int content_width = preview->width - PADDING * 2;
        int preview_height = app->height - STATUSBAR_HEIGHT - content_offset;

        // Calculate video area dimensions
        int draw_width = 0, draw_height = 0;
        if (preview->video_loaded && preview->image_width > 0) {
            float scale = 1.0f;
            if (preview->image_width > content_width) {
                scale = (float)content_width / preview->image_width;
            }
            int max_height = preview_height - PADDING * 4 - 120;
            if (preview->image_height * scale > max_height) {
                scale = (float)max_height / preview->image_height;
            }
            draw_width = (int)(preview->image_width * scale);
            draw_height = (int)(preview->image_height * scale);
        }

        // Calculate control button positions (must match preview_draw)
        int btn_size = 32;
        int btn_spacing = PADDING;
        int controls_width = btn_size * 2 + btn_spacing;
        int controls_x = content_x + (content_width - controls_width) / 2;
        int controls_y = content_y + draw_height + PADDING;

        Rectangle play_btn = {(float)controls_x, (float)controls_y, (float)btn_size, (float)btn_size};
        Rectangle stop_btn = {(float)(controls_x + btn_size + btn_spacing), (float)controls_y, (float)btn_size, (float)btn_size};

        // Play/Pause button click
        if (CheckCollisionPointRec(mouse, play_btn)) {
            if (!preview->video_inpane_active) {
                preview_start_video_playback(preview);
            } else {
                preview->video_paused = !preview->video_paused;
            }
        }

        // Stop button click
        if (CheckCollisionPointRec(mouse, stop_btn)) {
            if (preview->video_inpane_active) {
                preview_stop_video_playback(preview);
            }
        }

        // Also allow clicking on thumbnail/video area to play when not playing
        if (!preview->video_inpane_active && preview->video_loaded) {
            int draw_x = content_x + (content_width - draw_width) / 2;
            Rectangle thumb_rect = {(float)draw_x, (float)content_y, (float)draw_width, (float)draw_height};
            if (CheckCollisionPointRec(mouse, thumb_rect)) {
                preview_start_video_playback(preview);
            }
        }
    }

    // Update video texture from decode thread (thread does the reading)
    if (preview->video_inpane_active && preview->video_frame_texture_id != 0) {
        // Check if thread has a new frame ready
        pthread_mutex_lock(&preview->video_mutex);
        if (preview->video_frame_ready) {
            Texture2D tex = {
                .id = preview->video_frame_texture_id,
                .width = preview->video_frame_width,
                .height = preview->video_frame_height,
                .mipmaps = 1,
                .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8
            };
            UpdateTexture(tex, preview->video_frame_buffer);
            preview->video_frame_ready = false;
        }
        pthread_mutex_unlock(&preview->video_mutex);

        // Check if video ended
        if (preview->video_eof) {
            preview_stop_video_playback(preview);
        }
    }

    // Preview resize with mouse
    if (preview->visible) {
        Rectangle resize_area = {
            (float)(preview_x - 4),
            0,
            8,
            (float)app->height
        };

        if (CheckCollisionPointRec(mouse, resize_area)) {
            SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                preview->resizing = true;
            }
        } else if (!preview->resizing) {
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }

        if (preview->resizing) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                preview->width = app->width - (int)mouse.x;
                if (preview->width < PREVIEW_MIN_WIDTH) preview->width = PREVIEW_MIN_WIDTH;
                if (preview->width > PREVIEW_MAX_WIDTH) preview->width = PREVIEW_MAX_WIDTH;
            } else {
                preview->resizing = false;
                SetMouseCursor(MOUSE_CURSOR_DEFAULT);
            }
        }
    }

    return false;
}

void preview_draw(struct App *app)
{
    PreviewState *preview = &app->preview;
    BrowserState *bs = &app->browser_state;

    if (!preview->visible) return;

    int tab_height = tabs_get_height(&app->tabs);
    int search_height = search_is_active(&app->search) ? 32 : 0;
    int content_offset = tab_height + search_height;
    int preview_x = app->width - preview->width;
    int total_preview_height = app->height - STATUSBAR_HEIGHT - content_offset;

    // Check if summary pane should be visible
    bool show_summary_pane = (bs->summary_state == HOVER_LOADING ||
                              bs->summary_state == HOVER_READY ||
                              bs->summary_state == HOVER_ERROR);
    preview->summary_pane_visible = show_summary_pane;

    // Calculate heights based on summary pane visibility
    int summary_pane_height = show_summary_pane ? preview->summary_pane_height : 0;
    int preview_height = total_preview_height - summary_pane_height;

    // Background for main preview area
    DrawRectangle(preview_x, content_offset, preview->width, preview_height, g_theme.background);

    // Left border
    DrawLine(preview_x, content_offset, preview_x, content_offset + preview_height, g_theme.border);

    // Resize handle
    DrawRectangle(preview_x, content_offset, 2, preview_height, g_theme.border);

    // If no file selected or directory selected
    if (preview->type == PREVIEW_NONE) {
        DrawTextCustom("No preview", preview_x + PADDING, content_offset + PADDING, FONT_SIZE, g_theme.textSecondary);
        return;
    }

    int content_x = preview_x + PADDING;
    int content_y = content_offset + PADDING;
    int content_width = preview->width - PADDING * 2;

    // Draw based on type
    switch (preview->type) {
        case PREVIEW_IMAGE: {
            if (preview->image_loaded && preview->texture_id != 0) {
                Texture2D tex = {
                    .id = preview->texture_id,
                    .width = preview->image_width,
                    .height = preview->image_height,
                    .mipmaps = 1,
                    .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
                };

                // Scale to fit (leave room for metadata and edit UI)
                int max_img_height = preview_height - PADDING * 2 - 200;
                float scale = calculate_preview_scale(preview->image_width, preview->image_height,
                                                      content_width, max_img_height);

                int draw_width = (int)(preview->image_width * scale);
                int draw_height = (int)(preview->image_height * scale);
                int draw_x = content_x + (content_width - draw_width) / 2;
                int draw_y = content_y;

                DrawTextureEx(tex, (Vector2){(float)draw_x, (float)draw_y}, 0.0f, scale, WHITE);

                // Enhanced image metadata display
                int info_y = content_y + draw_height + PADDING;
                char info[256];

                // Filename
                const char *filename = strrchr(preview->file_path, '/');
                filename = filename ? filename + 1 : preview->file_path;
                DrawTextCustom(filename, content_x, info_y, FONT_SIZE_SMALL, g_theme.textPrimary);
                info_y += ROW_HEIGHT;

                // Format
                if (preview->image_format[0]) {
                    snprintf(info, sizeof(info), "Format: %s", preview->image_format);
                    DrawTextCustom(info, content_x, info_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                    info_y += ROW_HEIGHT;
                }

                // Resolution
                snprintf(info, sizeof(info), "Resolution: %dx%d", preview->image_width, preview->image_height);
                DrawTextCustom(info, content_x, info_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                info_y += ROW_HEIGHT;

                // Bit depth
                if (preview->image_bit_depth > 0) {
                    snprintf(info, sizeof(info), "Bit Depth: %d-bit", preview->image_bit_depth);
                    DrawTextCustom(info, content_x, info_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                    info_y += ROW_HEIGHT;
                }

                // ============================================================
                // IMAGE EDIT UI
                // ============================================================
                info_y += PADDING;

                // Draw separator
                DrawLine(content_x, info_y, content_x + content_width, info_y, g_theme.border);
                info_y += PADDING;

                // Draw "Edit Image" header with AI accent
                DrawTextCustom("AI Image Edit", content_x, info_y, FONT_SIZE, g_theme.aiAccent);
                info_y += ROW_HEIGHT;

                if (preview->edit_state == IMAGE_EDIT_NONE) {
                    // Show "Edit Image" button
                    ButtonResult btn = draw_button(content_x, info_y, 90, 24,
                                                   "Edit Image", g_theme.accent, g_theme.aiAccent);
                    if (btn.clicked) {
                        preview->edit_state = IMAGE_EDIT_INPUT;
                        preview->edit_buffer[0] = '\0';
                        preview->edit_cursor = 0;
                    }

                } else if (preview->edit_state == IMAGE_EDIT_INPUT) {
                    // Show text input for edit prompt
                    DrawTextCustom("Describe your edit:", content_x, info_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                    info_y += ROW_HEIGHT;

                    // Draw text input box
                    int input_height = 60;
                    Rectangle input_rect = {(float)content_x, (float)info_y, (float)content_width, (float)input_height};
                    DrawRectangleRec(input_rect, g_theme.sidebar);
                    DrawRectangleLinesEx(input_rect, 1, g_theme.accent);

                    // Draw input text with word wrap
                    int text_padding = 4;
                    int max_chars_per_line = (content_width - text_padding * 2) / 7;
                    char *buf = preview->edit_buffer;
                    int line = 0;
                    int line_start = 0;
                    int len = (int)strlen(buf);

                    for (int i = 0; i <= len && line < 3; i++) {
                        if (buf[i] == '\0' || i - line_start >= max_chars_per_line) {
                            char line_buf[256];
                            int line_len = i - line_start;
                            if (line_len > (int)sizeof(line_buf) - 1) line_len = (int)sizeof(line_buf) - 1;
                            strncpy(line_buf, buf + line_start, (size_t)line_len);
                            line_buf[line_len] = '\0';
                            DrawTextCustom(line_buf, content_x + text_padding,
                                     info_y + text_padding + line * (FONT_SIZE_SMALL + 2),
                                     FONT_SIZE_SMALL, g_theme.textPrimary);
                            line_start = i;
                            line++;
                        }
                    }

                    // Draw cursor (blinking)
                    static float cursor_timer = 0.0f;
                    cursor_timer += GetFrameTime();
                    if ((int)(cursor_timer * 2) % 2 == 0) {
                        int cursor_line = preview->edit_cursor / max_chars_per_line;
                        int line_start = cursor_line * max_chars_per_line;
                        int chars_to_cursor = preview->edit_cursor - line_start;

                        // Measure actual text width from line start to cursor
                        char cursor_text[PREVIEW_EDIT_BUFFER_SIZE];
                        if (chars_to_cursor > (int)sizeof(cursor_text) - 1) chars_to_cursor = (int)sizeof(cursor_text) - 1;
                        if (chars_to_cursor > 0) {
                            strncpy(cursor_text, preview->edit_buffer + line_start, (size_t)chars_to_cursor);
                        }
                        cursor_text[chars_to_cursor] = '\0';

                        int cursor_x = content_x + text_padding + MeasureTextCustom(cursor_text, FONT_SIZE_SMALL);
                        int cursor_y = info_y + text_padding + cursor_line * (FONT_SIZE_SMALL + 2);
                        DrawLine(cursor_x, cursor_y, cursor_x, cursor_y + FONT_SIZE_SMALL, g_theme.textPrimary);
                    }

                    info_y += input_height + PADDING;

                    // Draw Submit and Cancel buttons
                    ButtonResult submit = draw_button(content_x, info_y, 70, 24,
                                                      "Submit", g_theme.accent, g_theme.aiAccent);
                    ButtonResult cancel = draw_button(content_x + 78, info_y, 70, 24,
                                                      "Cancel", g_theme.textSecondary, g_theme.error);

                    if (submit.clicked && strlen(preview->edit_buffer) > 0) {
                        preview->edit_state = IMAGE_EDIT_LOADING;
                    }
                    if (cancel.clicked) {
                        preview->edit_state = IMAGE_EDIT_NONE;
                    }

                } else if (preview->edit_state == IMAGE_EDIT_LOADING) {
                    // Show loading spinner with message
                    int spinner_cx = content_x + (preview->width - PADDING * 2) / 2;
                    int spinner_cy = info_y + 30;
                    progress_indicator_draw_spinner(&preview->edit_progress, spinner_cx, spinner_cy, 15, g_theme.aiAccent);

                    // Draw message below spinner
                    Font font = font_get(FONT_SIZE_SMALL);
                    Vector2 msg_size = MeasureTextEx(font, preview->edit_progress.message, FONT_SIZE_SMALL, 1);
                    int msg_x = spinner_cx - (int)(msg_size.x / 2);
                    int msg_y = spinner_cy + 25;
                    DrawTextEx(font, preview->edit_progress.message, (Vector2){(float)msg_x, (float)msg_y},
                               FONT_SIZE_SMALL, 1, g_theme.textSecondary);
                    info_y += 80;

                } else if (preview->edit_state == IMAGE_EDIT_SUCCESS) {
                    // Show success message
                    DrawTextCustom("Edit complete!", content_x, info_y, FONT_SIZE_SMALL, g_theme.accent);
                    info_y += ROW_HEIGHT;

                    // Show path to new file
                    const char *result_name = strrchr(preview->edit_result_path, '/');
                    result_name = result_name ? result_name + 1 : preview->edit_result_path;
                    char result_msg[256];
                    snprintf(result_msg, sizeof(result_msg), "Saved: %s", result_name);
                    DrawTextCustom(result_msg, content_x, info_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                    info_y += ROW_HEIGHT + PADDING;

                    // "Edit Again" button
                    ButtonResult btn = draw_button(content_x, info_y, 90, 24,
                                                   "Edit Again", g_theme.accent, g_theme.aiAccent);
                    if (btn.clicked) {
                        preview->edit_state = IMAGE_EDIT_INPUT;
                        preview->edit_buffer[0] = '\0';
                        preview->edit_cursor = 0;
                    }

                } else if (preview->edit_state == IMAGE_EDIT_ERROR) {
                    // Show error message
                    DrawTextCustom("Edit failed", content_x, info_y, FONT_SIZE_SMALL, g_theme.error);
                    info_y += ROW_HEIGHT;

                    DrawTextCustom(preview->edit_error, content_x, info_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                    info_y += ROW_HEIGHT + PADDING;

                    // "Try Again" button
                    ButtonResult btn = draw_button(content_x, info_y, 90, 24,
                                                   "Try Again", g_theme.accent, g_theme.aiAccent);
                    if (btn.clicked) {
                        preview->edit_state = IMAGE_EDIT_INPUT;
                    }
                }
            }
            break;
        }

        case PREVIEW_TEXT:
        case PREVIEW_CODE:
        case PREVIEW_MARKDOWN: {
            // Draw "Summarize" button at top right
            int btn_width = 90;
            int btn_height = 24;
            int btn_x = preview_x + preview->width - btn_width - PADDING;
            int btn_y = content_y;
            Rectangle btn_rect = {(float)btn_x, (float)btn_y, (float)btn_width, (float)btn_height};

            bool btn_hovered = CheckCollisionPointRec(GetMousePosition(), btn_rect);
            Color btn_color = btn_hovered ? g_theme.aiAccent : g_theme.accent;

            // Determine button text based on state
            const char *btn_text = "Summarize";
            if (bs->summary_state == HOVER_LOADING) {
                btn_text = "Loading...";
                btn_color = g_theme.textSecondary;
            } else if (bs->summary_state == HOVER_READY &&
                       strcmp(bs->summary_path, preview->file_path) == 0) {
                btn_text = "Refresh";
            }

            DrawRectangleRec(btn_rect, btn_color);
            int text_width = MeasureTextCustom(btn_text, FONT_SIZE_SMALL);
            DrawTextCustom(btn_text, btn_x + (btn_width - text_width) / 2,
                     btn_y + (btn_height - FONT_SIZE_SMALL) / 2, FONT_SIZE_SMALL, g_theme.background);

            // Handle button click
            if (btn_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                bs->summary_state != HOVER_LOADING) {
                // Start async summary
                app->summary_config.default_level = SUMM_LEVEL_BRIEF;
                if (summarize_async_start(&app->async_summary_request, &app->summary_thread,
                                          preview->file_path, &app->summary_config, app->summary_cache)) {
                    atomic_store(&app->summary_thread_active, true);
                    bs->summary_state = HOVER_LOADING;
                    strncpy(bs->summary_path, preview->file_path, sizeof(bs->summary_path) - 1);
                }
            }

            // Adjust content_y to account for button
            int text_start_y = content_y + btn_height + PADDING;

            int line_height = FONT_SIZE_SMALL + 2;
            int available_height = preview_height - (text_start_y - content_offset) - PADDING;
            int visible_lines = available_height / line_height;

            // PREVIEW_TEXT: Word-wrapped prose view (no line numbers)
            if (preview->type == PREVIEW_TEXT && preview->wrapped_content) {
                // Draw wrapped text with scrolling
                draw_text_wrapped_scrolled(
                    preview->wrapped_content,
                    content_x, text_start_y,
                    content_width,
                    visible_lines,
                    preview->scroll_offset,
                    FONT_SIZE_SMALL, g_theme.textPrimary
                );

                // Scroll indicator for wrapped content
                if (preview->wrapped_total_lines > visible_lines) {
                    int scrollbar_height = available_height;
                    int thumb_height = (visible_lines * scrollbar_height) / preview->wrapped_total_lines;
                    if (thumb_height < 20) thumb_height = 20;

                    int max_scroll = preview->wrapped_total_lines - visible_lines;
                    int thumb_y = text_start_y;
                    if (max_scroll > 0) {
                        thumb_y = text_start_y + (preview->scroll_offset * (scrollbar_height - thumb_height)) / max_scroll;
                    }

                    DrawRectangle(preview_x + preview->width - 8, text_start_y, 4, scrollbar_height, g_theme.sidebar);
                    DrawRectangle(preview_x + preview->width - 8, thumb_y, 4, thumb_height, g_theme.selection);
                }
            }
            // PREVIEW_CODE/PREVIEW_MARKDOWN: Line-based view with line numbers
            else if (preview->text_content) {
                // Get lines
                char *line_start = preview->text_content;
                int line_num = 0;

                // Skip to scroll offset
                while (*line_start && line_num < preview->scroll_offset) {
                    if (*line_start == '\n') line_num++;
                    line_start++;
                }

                int drawn = 0;
                char line_buffer[256];
                int y = text_start_y;

                while (*line_start && drawn < visible_lines) {
                    char *line_end = strchr(line_start, '\n');
                    int len = line_end ? (int)(line_end - line_start) : (int)strlen(line_start);

                    if (len > (int)sizeof(line_buffer) - 1) {
                        len = (int)sizeof(line_buffer) - 1;
                    }

                    strncpy(line_buffer, line_start, len);
                    line_buffer[len] = '\0';

                    // Truncate if too long
                    int max_chars = (content_width - 30) / 7;
                    if ((int)strlen(line_buffer) > max_chars && max_chars > 3) {
                        line_buffer[max_chars - 3] = '.';
                        line_buffer[max_chars - 2] = '.';
                        line_buffer[max_chars - 1] = '.';
                        line_buffer[max_chars] = '\0';
                    }

                    // Line number
                    char line_num_str[8];
                    snprintf(line_num_str, sizeof(line_num_str), "%3d", preview->scroll_offset + drawn + 1);
                    DrawTextCustom(line_num_str, content_x, y, FONT_SIZE_SMALL, g_theme.textSecondary);

                    // Line content
                    DrawTextCustom(line_buffer, content_x + 30, y, FONT_SIZE_SMALL, g_theme.textPrimary);

                    y += line_height;
                    drawn++;

                    if (line_end) {
                        line_start = line_end + 1;
                    } else {
                        break;
                    }
                }

                // Scroll indicator
                if (preview->text_lines > visible_lines) {
                    int scrollbar_height = available_height;
                    int thumb_height = (visible_lines * scrollbar_height) / preview->text_lines;
                    if (thumb_height < 20) thumb_height = 20;

                    int thumb_y = text_start_y + (preview->scroll_offset * (scrollbar_height - thumb_height)) / (preview->text_lines - visible_lines);

                    DrawRectangle(preview_x + preview->width - 8, text_start_y, 4, scrollbar_height, g_theme.sidebar);
                    DrawRectangle(preview_x + preview->width - 8, thumb_y, 4, thumb_height, g_theme.selection);
                }
            }
            break;
        }

        case PREVIEW_PDF:
            DrawTextCustom("PDF Preview", content_x, content_y, FONT_SIZE, g_theme.textPrimary);
            DrawTextCustom("(not yet supported)", content_x, content_y + ROW_HEIGHT, FONT_SIZE_SMALL, g_theme.textSecondary);
            break;

        case PREVIEW_VIDEO: {
            int draw_y = content_y;
            int draw_width = 0;
            int draw_height = 0;
            int draw_x = content_x;

            // Check if in-pane playback is active and we have frames
            if (preview->video_inpane_active && preview->video_frame_texture_id != 0) {
                // Playing video in-pane: render current frame
                Texture2D tex = {
                    .id = preview->video_frame_texture_id,
                    .width = preview->video_frame_width,
                    .height = preview->video_frame_height,
                    .mipmaps = 1,
                    .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8
                };

                // Scale frame to fit
                float scale = 1.0f;
                if (preview->video_frame_width > content_width) {
                    scale = (float)content_width / preview->video_frame_width;
                }
                int max_height = preview_height - PADDING * 4 - 120;  // Leave room for controls and info
                if (preview->video_frame_height * scale > max_height) {
                    scale = (float)max_height / preview->video_frame_height;
                }

                draw_width = (int)(preview->video_frame_width * scale);
                draw_height = (int)(preview->video_frame_height * scale);
                draw_x = content_x + (content_width - draw_width) / 2;

                DrawTextureEx(tex, (Vector2){(float)draw_x, (float)draw_y}, 0.0f, scale, WHITE);
                draw_y += draw_height + PADDING;

            } else if (preview->video_loaded && preview->video_thumbnail_id != 0) {
                // Not playing: show thumbnail
                Texture2D tex = {
                    .id = preview->video_thumbnail_id,
                    .width = preview->image_width,
                    .height = preview->image_height,
                    .mipmaps = 1,
                    .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
                };

                // Scale thumbnail to fit
                float scale = 1.0f;
                if (preview->image_width > content_width) {
                    scale = (float)content_width / preview->image_width;
                }
                int max_height = preview_height - PADDING * 4 - 120;  // Leave room for controls and info
                if (preview->image_height * scale > max_height) {
                    scale = (float)max_height / preview->image_height;
                }

                draw_width = (int)(preview->image_width * scale);
                draw_height = (int)(preview->image_height * scale);
                draw_x = content_x + (content_width - draw_width) / 2;

                DrawTextureEx(tex, (Vector2){(float)draw_x, (float)draw_y}, 0.0f, scale, WHITE);

                // Draw play button overlay when not playing
                if (!preview->video_inpane_active) {
                    int center_x = draw_x + draw_width / 2;
                    int center_y = draw_y + draw_height / 2;
                    int button_radius = 30;

                    DrawCircle(center_x, center_y, button_radius, (Color){0, 0, 0, 150});
                    DrawCircleLines(center_x, center_y, button_radius, WHITE);

                    Vector2 v1 = {(float)(center_x - 10), (float)(center_y - 15)};
                    Vector2 v2 = {(float)(center_x - 10), (float)(center_y + 15)};
                    Vector2 v3 = {(float)(center_x + 15), (float)(center_y)};
                    DrawTriangle(v1, v2, v3, WHITE);
                }

                draw_y += draw_height + PADDING;
            } else {
                // No thumbnail available
                DrawTextCustom("Video Preview", content_x, draw_y, FONT_SIZE, g_theme.textPrimary);
                draw_y += ROW_HEIGHT;
                DrawTextCustom("(thumbnail unavailable)", content_x, draw_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                draw_y += ROW_HEIGHT + PADDING;
            }

            // Draw playback controls: [Play/Pause] [Stop]
            int btn_size = 32;
            int btn_spacing = PADDING;
            int controls_width = btn_size * 2 + btn_spacing;
            int controls_x = content_x + (content_width - controls_width) / 2;

            // Play/Pause button
            Rectangle play_btn = {(float)controls_x, (float)draw_y, (float)btn_size, (float)btn_size};
            bool play_hover = CheckCollisionPointRec(GetMousePosition(), play_btn);
            DrawRectangleRec(play_btn, play_hover ? g_theme.hover : g_theme.sidebar);
            DrawRectangleLinesEx(play_btn, 1, g_theme.border);

            if (preview->video_inpane_active && !preview->video_paused) {
                // Draw pause icon (two vertical bars)
                int bar_width = 4;
                int bar_height = 16;
                int bar_y = draw_y + (btn_size - bar_height) / 2;
                DrawRectangle(controls_x + btn_size/2 - bar_width - 2, bar_y, bar_width, bar_height, WHITE);
                DrawRectangle(controls_x + btn_size/2 + 2, bar_y, bar_width, bar_height, WHITE);
            } else {
                // Draw play triangle
                int tri_x = controls_x + btn_size/2 - 4;
                int tri_y = draw_y + btn_size/2;
                Vector2 pv1 = {(float)(tri_x - 4), (float)(tri_y - 8)};
                Vector2 pv2 = {(float)(tri_x - 4), (float)(tri_y + 8)};
                Vector2 pv3 = {(float)(tri_x + 8), (float)(tri_y)};
                DrawTriangle(pv1, pv2, pv3, WHITE);
            }

            // Stop button
            Rectangle stop_btn = {(float)(controls_x + btn_size + btn_spacing), (float)draw_y, (float)btn_size, (float)btn_size};
            bool stop_hover = CheckCollisionPointRec(GetMousePosition(), stop_btn);
            DrawRectangleRec(stop_btn, stop_hover ? g_theme.hover : g_theme.sidebar);
            DrawRectangleLinesEx(stop_btn, 1, g_theme.border);

            // Draw stop square
            int sq_size = 12;
            int sq_x = controls_x + btn_size + btn_spacing + (btn_size - sq_size) / 2;
            int sq_y = draw_y + (btn_size - sq_size) / 2;
            DrawRectangle(sq_x, sq_y, sq_size, sq_size, WHITE);

            draw_y += btn_size + PADDING;

            // Enhanced video metadata display
            char info[256];

            // Filename
            const char *filename = strrchr(preview->file_path, '/');
            filename = filename ? filename + 1 : preview->file_path;
            DrawTextCustom(filename, content_x, draw_y, FONT_SIZE_SMALL, g_theme.textPrimary);
            draw_y += ROW_HEIGHT;

            // Format (codec)
            if (preview->video_format[0]) {
                snprintf(info, sizeof(info), "Format: %s", preview->video_format);
                DrawTextCustom(info, content_x, draw_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                draw_y += ROW_HEIGHT;
            }

            // Resolution
            if (preview->video_width > 0 && preview->video_height > 0) {
                snprintf(info, sizeof(info), "Resolution: %dx%d", preview->video_width, preview->video_height);
                DrawTextCustom(info, content_x, draw_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                draw_y += ROW_HEIGHT;
            }

            // Bit depth
            if (preview->video_bit_depth > 0) {
                snprintf(info, sizeof(info), "Bit Depth: %d-bit", preview->video_bit_depth);
                DrawTextCustom(info, content_x, draw_y, FONT_SIZE_SMALL, g_theme.textSecondary);
                draw_y += ROW_HEIGHT;
            }

            // Duration
            if (preview->video_duration > 0) {
                int mins = (int)(preview->video_duration / 60);
                int secs = (int)(preview->video_duration) % 60;
                snprintf(info, sizeof(info), "Duration: %d:%02d", mins, secs);
                DrawTextCustom(info, content_x, draw_y, FONT_SIZE_SMALL, g_theme.textSecondary);
            }
            break;
        }

        case PREVIEW_UNKNOWN:
        default: {
            DrawTextCustom("File Info", content_x, content_y, FONT_SIZE, g_theme.textPrimary);
            content_y += ROW_HEIGHT + PADDING;

            // Get file info
            const char *name = strrchr(preview->file_path, '/');
            if (name) name++;
            else name = preview->file_path;

            DrawTextCustom(name, content_x, content_y, FONT_SIZE_SMALL, g_theme.textSecondary);
            content_y += ROW_HEIGHT;

            DrawTextCustom("Type: Unknown", content_x, content_y, FONT_SIZE_SMALL, g_theme.textSecondary);
            break;
        }
    }

    // =========================================================================
    // AI SUMMARY PANE (separate panel at bottom)
    // =========================================================================
    if (show_summary_pane) {
        // Calculate summary pane position (below main preview)
        int summary_pane_y = content_offset + preview_height;
        int summary_x = preview_x + PADDING;
        int summary_width = preview->width - PADDING * 2;

        // Draw summary pane background with distinct color
        DrawRectangle(preview_x, summary_pane_y, preview->width, summary_pane_height, g_theme.sidebar);

        // Draw top border separator
        DrawLine(preview_x, summary_pane_y, preview_x + preview->width, summary_pane_y, g_theme.border);

        // Draw left border (continuation of preview border)
        DrawLine(preview_x, summary_pane_y, preview_x, summary_pane_y + summary_pane_height, g_theme.border);

        int text_y = summary_pane_y + PADDING;

        // Draw AI Summary header with AI accent color
        DrawTextCustom("AI Summary", summary_x, text_y, FONT_SIZE, g_theme.aiAccent);
        text_y += ROW_HEIGHT;

        if (bs->summary_state == HOVER_LOADING) {
            // Show loading indicator with animated dots
            static float dots_timer = 0.0f;
            dots_timer += GetFrameTime();
            int num_dots = ((int)(dots_timer * 3) % 4);
            char loading_text[32] = "Loading summary";
            for (int i = 0; i < num_dots; i++) strcat(loading_text, ".");
            DrawTextCustom(loading_text, summary_x, text_y, FONT_SIZE_SMALL, g_theme.textSecondary);
            // Reset scroll when loading new summary
            preview->summary_scroll_offset = 0;
            preview->summary_total_lines = 0;
        } else if (bs->summary_state == HOVER_READY) {
            // Calculate available lines in pane
            int line_height = FONT_SIZE_SMALL + 2;
            int available_height = summary_pane_height - ROW_HEIGHT - PADDING * 3;
            int visible_lines = available_height / line_height;
            if (visible_lines < 1) visible_lines = 1;

            // Calculate total lines for scrollbar (cache it)
            int total_lines = measure_text_lines(bs->summary_text, summary_width, FONT_SIZE_SMALL);
            preview->summary_total_lines = total_lines;

            // Draw scrollable text
            draw_text_wrapped_scrolled(bs->summary_text, summary_x, text_y,
                                       summary_width, visible_lines,
                                       preview->summary_scroll_offset,
                                       FONT_SIZE_SMALL, g_theme.textPrimary);

            // Draw scrollbar if content exceeds visible area
            if (total_lines > visible_lines) {
                int scrollbar_x = preview_x + preview->width - 8;
                int scrollbar_y = text_y;
                int scrollbar_height = available_height;

                int thumb_height = (visible_lines * scrollbar_height) / total_lines;
                if (thumb_height < 20) thumb_height = 20;

                int max_scroll = total_lines - visible_lines;
                int thumb_y = scrollbar_y;
                if (max_scroll > 0) {
                    thumb_y = scrollbar_y + (preview->summary_scroll_offset * (scrollbar_height - thumb_height)) / max_scroll;
                }

                // Scrollbar track
                DrawRectangle(scrollbar_x, scrollbar_y, 4, scrollbar_height, g_theme.background);
                // Scrollbar thumb
                DrawRectangle(scrollbar_x, thumb_y, 4, thumb_height, g_theme.selection);
            }
        } else if (bs->summary_state == HOVER_ERROR) {
            DrawTextCustom(bs->summary_error, summary_x, text_y, FONT_SIZE_SMALL, g_theme.error);
        }
    }
}

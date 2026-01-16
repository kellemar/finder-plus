#ifndef VIDEO_H
#define VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define VIDEO_CACHE_DIR ".cache/finder-plus/thumbnails"
#define VIDEO_THUMBNAIL_WIDTH 400

// Generate thumbnail for video file
// Returns true on success, false on failure
// Thumbnail is cached to ~/.cache/finder-plus/thumbnails/<hash>.png
bool video_generate_thumbnail(const char *video_path, char *thumbnail_path_out, size_t path_size);

// Get video metadata (duration, dimensions)
// Returns true on success, any output param can be NULL to skip
bool video_get_metadata(const char *video_path, float *duration_out, int *width_out, int *height_out);

// Start video playback using ffplay
// Returns PID of ffplay process, or -1 on failure
pid_t video_start_playback(const char *video_path);

// Stop video playback
void video_stop_playback(pid_t pid);

// Check if ffmpeg/ffplay is available
bool video_check_ffmpeg_available(void);

// Get thumbnail cache path for a video (deterministic based on file path hash)
void video_get_cache_path(const char *video_path, char *cache_path_out, size_t path_size);

// Check if a file extension is a supported video format
bool video_is_supported_format(const char *extension);

// Get extended video metadata (codec name, bit depth)
// Returns true on success, any output param can be NULL to skip
bool video_get_extended_metadata(const char *video_path, char *codec_out, size_t codec_size, int *bit_depth_out);

// Get video frame rate (FPS)
bool video_get_fps(const char *video_path, float *fps_out);

// Start in-pane video playback using ffmpeg frame extraction
// Returns file descriptor for reading frames, or -1 on failure
// Caller is responsible for closing the pipe and stopping playback
int video_start_inpane_playback(const char *video_path, int width, int height, pid_t *pid_out, float *fps_out);

// Read a single frame from the video pipe
// Returns true if a frame was read, false on EOF or error
bool video_read_frame(int pipe_fd, unsigned char *buffer, int width, int height);

// Stop in-pane video playback
void video_stop_inpane_playback(pid_t pid, int pipe_fd);

#endif // VIDEO_H

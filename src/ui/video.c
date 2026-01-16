#include "video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <CommonCrypto/CommonDigest.h>

// Supported video extensions
static const char *SUPPORTED_EXTENSIONS[] = {
    "mp4", "mov", "mkv", "avi", "webm", "m4v", NULL
};

// Helper: Convert string to uppercase in place
static void str_to_upper(char *str)
{
    if (!str) return;
    for (size_t i = 0; str[i]; i++) {
        str[i] = toupper((unsigned char)str[i]);
    }
}

// Helper: redirect stderr to /dev/null
static void redirect_stderr_to_null(void)
{
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }
}

// Helper: check if file exists and has content
static bool file_exists_with_content(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0;
}

// Compute MD5 hash of path for cache filename
// Note: MD5 is used for cache key generation, not security
static void compute_path_hash(const char *path, char *hash_out)
{
    if (!path || !hash_out) {
        if (hash_out) hash_out[0] = '\0';
        return;
    }

    unsigned char digest[CC_MD5_DIGEST_LENGTH];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_MD5(path, (CC_LONG)strlen(path), digest);
#pragma clang diagnostic pop

    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; i++) {
        snprintf(hash_out + (i * 2), 3, "%02x", digest[i]);
    }
    hash_out[CC_MD5_DIGEST_LENGTH * 2] = '\0';
}

void video_get_cache_path(const char *video_path, char *cache_path_out, size_t path_size)
{
    if (!cache_path_out || path_size == 0) return;

    if (!video_path) {
        cache_path_out[0] = '\0';
        return;
    }

    const char *home = getenv("HOME");
    char hash[33];
    compute_path_hash(video_path, hash);

    int written = snprintf(cache_path_out, path_size, "%s/%s/%s.png",
                          home ? home : "/tmp", VIDEO_CACHE_DIR, hash);

    // Check for truncation
    if (written < 0 || (size_t)written >= path_size) {
        cache_path_out[0] = '\0';
    }
}

// Ensure cache directory exists using mkdir() syscall (no shell)
static bool ensure_cache_dir(void)
{
    const char *home = getenv("HOME");
    if (!home) return false;

    char path[4096];
    int written;

    // Create ~/.cache
    written = snprintf(path, sizeof(path), "%s/.cache", home);
    if (written < 0 || (size_t)written >= sizeof(path)) return false;
    mkdir(path, 0755);  // Ignore EEXIST

    // Create ~/.cache/finder-plus
    written = snprintf(path, sizeof(path), "%s/.cache/finder-plus", home);
    if (written < 0 || (size_t)written >= sizeof(path)) return false;
    mkdir(path, 0755);

    // Create ~/.cache/finder-plus/thumbnails
    written = snprintf(path, sizeof(path), "%s/%s", home, VIDEO_CACHE_DIR);
    if (written < 0 || (size_t)written >= sizeof(path)) return false;

    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

// Run ffmpeg to generate thumbnail (no shell, uses fork/exec)
static bool run_ffmpeg_thumbnail(const char *video_path, const char *output_path,
                                  int width, const char *timestamp)
{
    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        // Child process
        redirect_stderr_to_null();

        char scale_filter[64];
        snprintf(scale_filter, sizeof(scale_filter), "scale='min(%d,iw)':-1", width);

        execlp("ffmpeg", "ffmpeg",
               "-y",
               "-i", video_path,
               "-ss", timestamp,
               "-vframes", "1",
               "-vf", scale_filter,
               output_path,
               NULL);
        _exit(1);
    }

    // Parent waits for child
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool video_generate_thumbnail(const char *video_path, char *thumbnail_path_out, size_t path_size)
{
    if (!video_path || !thumbnail_path_out || path_size == 0) {
        return false;
    }

    // Get cache path
    video_get_cache_path(video_path, thumbnail_path_out, path_size);
    if (thumbnail_path_out[0] == '\0') {
        return false;
    }

    // Check if already cached
    if (file_exists_with_content(thumbnail_path_out)) {
        return true;
    }

    // Ensure cache directory exists
    if (!ensure_cache_dir()) {
        return false;
    }

    // Try to extract frame at 1 second
    if (run_ffmpeg_thumbnail(video_path, thumbnail_path_out, VIDEO_THUMBNAIL_WIDTH, "00:00:01") &&
        file_exists_with_content(thumbnail_path_out)) {
        return true;
    }

    // Fallback: try at 0 seconds for short videos
    return run_ffmpeg_thumbnail(video_path, thumbnail_path_out, VIDEO_THUMBNAIL_WIDTH, "00:00:00") &&
           file_exists_with_content(thumbnail_path_out);
}

// Run ffprobe and capture output (no shell, uses fork/exec with pipe)
static bool run_ffprobe(const char *video_path, const char *const args[],
                        char *output, size_t output_size)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);  // Close read end
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        redirect_stderr_to_null();

        // Build full argument list with video_path
        // args should end with NULL, we insert video_path before it
        int argc = 0;
        while (args[argc]) argc++;

        // Use stack allocation (safe: argc bounded by caller's args array)
        const char *full_args[32];
        if (argc + 2 > 32) _exit(1);  // Safety check

        for (int i = 0; i < argc; i++) {
            full_args[i] = args[i];
        }
        full_args[argc] = video_path;
        full_args[argc + 1] = NULL;

        execvp("ffprobe", (char *const *)full_args);
        _exit(1);
    }

    // Parent process
    close(pipefd[1]);  // Close write end

    // Read output
    size_t total_read = 0;
    ssize_t n;
    while (total_read < output_size - 1 &&
           (n = read(pipefd[0], output + total_read, output_size - 1 - total_read)) > 0) {
        total_read += n;
    }
    output[total_read] = '\0';
    close(pipefd[0]);

    // Wait for child
    int status;
    waitpid(pid, &status, 0);

    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && total_read > 0;
}

bool video_get_metadata(const char *video_path, float *duration_out, int *width_out, int *height_out)
{
    if (!video_path) return false;

    char output[256];
    bool success = false;

    // Get stream info (width, height, duration)
    const char *stream_args[] = {
        "ffprobe", "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=width,height,duration",
        "-of", "csv=p=0",
        NULL
    };

    if (run_ffprobe(video_path, stream_args, output, sizeof(output))) {
        int w = 0, h = 0;
        float d = 0;

        if (sscanf(output, "%d,%d,%f", &w, &h, &d) >= 2) {
            if (width_out) *width_out = w;
            if (height_out) *height_out = h;
            if (duration_out) *duration_out = d;
            success = true;
        }
    }

    // If duration wasn't in stream info, try format duration
    if (success && duration_out && *duration_out <= 0) {
        const char *format_args[] = {
            "ffprobe", "-v", "error",
            "-show_entries", "format=duration",
            "-of", "csv=p=0",
            NULL
        };

        if (run_ffprobe(video_path, format_args, output, sizeof(output))) {
            float d = 0;
            if (sscanf(output, "%f", &d) == 1) {
                *duration_out = d;
            }
        }
    }

    return success;
}

pid_t video_start_playback(const char *video_path)
{
    if (!video_path) return -1;

    pid_t pid = fork();
    if (pid == 0) {
        // Child process - run ffplay
        // -autoexit: exit when video ends
        // -window_title: set window title
        // -x, -y: window size
        execlp("ffplay", "ffplay",
               "-autoexit",
               "-window_title", "Finder Plus - Video Preview",
               "-x", "640", "-y", "480",
               video_path,
               NULL);
        _exit(1);  // exec failed
    }

    return pid;
}

void video_stop_playback(pid_t pid)
{
    if (pid <= 0) return;

    kill(pid, SIGTERM);

    // Try non-blocking wait with timeout (1 second total)
    for (int i = 0; i < 10; i++) {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid || result == -1) return;  // Exited or error
        usleep(100000);  // 100ms
    }

    // Force kill if still running
    kill(pid, SIGKILL);
    int status;
    waitpid(pid, &status, 0);
}

bool video_check_ffmpeg_available(void)
{
    // Check common installation paths on macOS
    const char *paths[] = {
        "/opt/homebrew/bin/ffmpeg",  // Apple Silicon Homebrew
        "/usr/local/bin/ffmpeg",      // Intel Homebrew
        "/usr/bin/ffmpeg",            // System
        "/opt/local/bin/ffmpeg",      // MacPorts
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], X_OK) == 0) {
            return true;
        }
    }
    return false;
}

bool video_is_supported_format(const char *extension)
{
    if (!extension) return false;

    // Skip leading dot if present
    if (extension[0] == '.') extension++;

    // Convert to lowercase for comparison
    char ext_lower[16];
    size_t len = strlen(extension);
    if (len >= sizeof(ext_lower)) return false;

    for (size_t i = 0; i <= len; i++) {
        ext_lower[i] = (char)tolower((unsigned char)extension[i]);
    }

    // Check against supported extensions
    for (int i = 0; SUPPORTED_EXTENSIONS[i]; i++) {
        if (strcmp(ext_lower, SUPPORTED_EXTENSIONS[i]) == 0) {
            return true;
        }
    }

    return false;
}

bool video_get_extended_metadata(const char *video_path, char *codec_out, size_t codec_size, int *bit_depth_out)
{
    if (!video_path) return false;

    char output[256];

    const char *codec_args[] = {
        "ffprobe", "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=codec_name,bits_per_raw_sample",
        "-of", "csv=p=0",
        NULL
    };

    if (!run_ffprobe(video_path, codec_args, output, sizeof(output))) {
        return false;
    }

    // Parse "h264,8" or "hevc,10" format
    char *newline = strchr(output, '\n');
    if (newline) *newline = '\0';

    if (output[0] == '\0') {
        return false;
    }

    // Parse bit depth if comma present
    char *comma = strchr(output, ',');
    if (comma) {
        *comma = '\0';
        if (bit_depth_out) {
            int bd = atoi(comma + 1);
            *bit_depth_out = (bd > 0) ? bd : 8;
        }
    } else if (bit_depth_out) {
        *bit_depth_out = 8;  // Default to 8-bit
    }

    // Copy and uppercase codec name
    if (codec_out && codec_size > 0) {
        strncpy(codec_out, output, codec_size - 1);
        codec_out[codec_size - 1] = '\0';
        str_to_upper(codec_out);
    }

    return true;
}

bool video_get_fps(const char *video_path, float *fps_out)
{
    if (!video_path || !fps_out) return false;

    char output[256];

    const char *fps_args[] = {
        "ffprobe", "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=r_frame_rate",
        "-of", "csv=p=0",
        NULL
    };

    if (run_ffprobe(video_path, fps_args, output, sizeof(output))) {
        // Parse "30/1" or "30000/1001" format
        int num = 0, den = 1;
        if (sscanf(output, "%d/%d", &num, &den) == 2 && den > 0) {
            *fps_out = (float)num / (float)den;
            return true;
        } else if (sscanf(output, "%d", &num) == 1) {
            *fps_out = (float)num;
            return true;
        }
    }

    *fps_out = 30.0f;  // Default fallback
    return false;
}

int video_start_inpane_playback(const char *video_path, int width, int height, pid_t *pid_out, float *fps_out)
{
    if (!video_path || !pid_out) return -1;

    // Get FPS first
    if (fps_out) {
        video_get_fps(video_path, fps_out);
    }

    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process - run ffmpeg to output raw RGB frames
        close(pipefd[0]);  // Close read end
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        redirect_stderr_to_null();

        char scale[64];
        snprintf(scale, sizeof(scale), "scale=%d:%d", width, height);

        execlp("ffmpeg", "ffmpeg",
               "-i", video_path,
               "-an",                    // Disable audio
               "-vf", scale,
               "-f", "rawvideo",
               "-pix_fmt", "rgb24",
               "-vsync", "0",            // Output frames as fast as decoded
               "-",
               NULL);
        _exit(1);  // exec failed
    }

    // Parent process
    close(pipefd[1]);  // Close write end
    *pid_out = pid;
    return pipefd[0];  // Return read end
}

bool video_read_frame(int pipe_fd, unsigned char *buffer, int width, int height)
{
    if (pipe_fd < 0 || !buffer || width <= 0 || height <= 0) return false;

    size_t frame_size = (size_t)(width * height * 3);  // RGB24
    size_t total_read = 0;

    while (total_read < frame_size) {
        ssize_t n = read(pipe_fd, buffer + total_read, frame_size - total_read);
        if (n <= 0) {
            // EOF or error
            return false;
        }
        total_read += n;
    }

    return true;
}

void video_stop_inpane_playback(pid_t pid, int pipe_fd)
{
    if (pipe_fd >= 0) {
        close(pipe_fd);
    }

    if (pid > 0) {
        kill(pid, SIGTERM);

        // Try non-blocking wait with short timeout
        for (int i = 0; i < 5; i++) {
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid || result == -1) return;
            usleep(50000);  // 50ms
        }

        // Force kill if still running
        kill(pid, SIGKILL);
        int status;
        waitpid(pid, &status, 0);
    }
}

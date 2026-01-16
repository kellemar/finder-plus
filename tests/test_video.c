#include <stdio.h>
#include <string.h>
#include "ui/video.h"

// Import test macros
extern void inc_tests_run(void);
extern void inc_tests_passed(void);
extern void inc_tests_failed(void);

#define TEST_ASSERT(condition, message) do { \
    inc_tests_run(); \
    if (condition) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s (line %d)\n", message, __LINE__); \
    } \
} while(0)

#define TEST_ASSERT_STR_EQ(expected, actual, message) do { \
    inc_tests_run(); \
    if (strcmp((expected), (actual)) == 0) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s - expected '%s', got '%s' (line %d)\n", message, (expected), (actual), __LINE__); \
    } \
} while(0)

void test_video(void)
{
    // Test 1: Cache path generation is deterministic
    char path1[4096], path2[4096];
    video_get_cache_path("/test/video.mp4", path1, sizeof(path1));
    video_get_cache_path("/test/video.mp4", path2, sizeof(path2));
    TEST_ASSERT_STR_EQ(path1, path2, "Same input produces same cache path");

    // Test 2: Different files get different cache paths
    char path3[4096];
    video_get_cache_path("/other/video.mp4", path3, sizeof(path3));
    TEST_ASSERT(strcmp(path1, path3) != 0, "Different files get different cache paths");

    // Test 3: NULL handling in cache path
    char path4[4096];
    path4[0] = 'X';  // Set to non-null to verify it gets cleared
    video_get_cache_path(NULL, path4, sizeof(path4));
    TEST_ASSERT(path4[0] == '\0', "NULL video path returns empty cache path");

    // Test 4: Empty buffer handling
    video_get_cache_path("/test/video.mp4", NULL, 0);  // Should not crash
    TEST_ASSERT(1, "NULL buffer handling does not crash");

    // Test 5: Supported format detection
    TEST_ASSERT(video_is_supported_format("mp4") == true, "mp4 is supported");
    TEST_ASSERT(video_is_supported_format("MP4") == true, "MP4 (uppercase) is supported");
    TEST_ASSERT(video_is_supported_format("mov") == true, "mov is supported");
    TEST_ASSERT(video_is_supported_format("mkv") == true, "mkv is supported");
    TEST_ASSERT(video_is_supported_format("avi") == true, "avi is supported");
    TEST_ASSERT(video_is_supported_format("webm") == true, "webm is supported");
    TEST_ASSERT(video_is_supported_format("m4v") == true, "m4v is supported");

    // Test 6: Unsupported format detection
    TEST_ASSERT(video_is_supported_format("txt") == false, "txt is not supported");
    TEST_ASSERT(video_is_supported_format("pdf") == false, "pdf is not supported");
    TEST_ASSERT(video_is_supported_format("png") == false, "png is not supported");
    TEST_ASSERT(video_is_supported_format("") == false, "empty string is not supported");
    TEST_ASSERT(video_is_supported_format(NULL) == false, "NULL is not supported");

    // Test 7: Extension with leading dot
    TEST_ASSERT(video_is_supported_format(".mp4") == true, ".mp4 (with dot) is supported");
    TEST_ASSERT(video_is_supported_format(".MOV") == true, ".MOV (with dot, uppercase) is supported");

    // Test 8: ffmpeg availability check (may or may not be installed)
    bool ffmpeg_available = video_check_ffmpeg_available();
    printf("  INFO: ffmpeg available: %s\n", ffmpeg_available ? "yes" : "no");
    TEST_ASSERT(1, "ffmpeg check does not crash");  // Just verify it doesn't crash

    // Test 9: Extended metadata NULL handling
    char codec[16];
    int bit_depth;
    bool result = video_get_extended_metadata(NULL, codec, sizeof(codec), &bit_depth);
    TEST_ASSERT(result == false, "Extended metadata with NULL path returns false");

    // Test 10: Extended metadata with NULL output params
    result = video_get_extended_metadata("/nonexistent.mp4", NULL, 0, NULL);
    TEST_ASSERT(1, "Extended metadata with NULL outputs does not crash");

    // Test 11: FPS extraction NULL handling
    float fps;
    result = video_get_fps(NULL, &fps);
    TEST_ASSERT(result == false, "FPS extraction with NULL path returns false");

    result = video_get_fps("/test.mp4", NULL);
    TEST_ASSERT(result == false, "FPS extraction with NULL output returns false");

    // Test 12: In-pane playback NULL handling
    pid_t pid;
    int pipe_fd = video_start_inpane_playback(NULL, 640, 480, &pid, &fps);
    TEST_ASSERT(pipe_fd == -1, "In-pane playback with NULL path returns -1");

    pipe_fd = video_start_inpane_playback("/test.mp4", 640, 480, NULL, &fps);
    TEST_ASSERT(pipe_fd == -1, "In-pane playback with NULL pid_out returns -1");

    // Test 13: Frame reading NULL handling
    unsigned char buffer[1024];
    result = video_read_frame(-1, buffer, 10, 10);
    TEST_ASSERT(result == false, "Frame read with invalid fd returns false");

    result = video_read_frame(0, NULL, 10, 10);
    TEST_ASSERT(result == false, "Frame read with NULL buffer returns false");

    result = video_read_frame(0, buffer, 0, 10);
    TEST_ASSERT(result == false, "Frame read with zero width returns false");

    result = video_read_frame(0, buffer, 10, 0);
    TEST_ASSERT(result == false, "Frame read with zero height returns false");

    // Test 14: Stop in-pane playback NULL handling (should not crash)
    video_stop_inpane_playback(0, -1);
    TEST_ASSERT(1, "Stop in-pane playback with invalid params does not crash");
}

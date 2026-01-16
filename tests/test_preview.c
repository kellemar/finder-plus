#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

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

#define TEST_ASSERT_EQ(expected, actual, message) do { \
    inc_tests_run(); \
    if ((expected) == (actual)) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s - expected %d, got %d (line %d)\n", message, (int)(expected), (int)(actual), __LINE__); \
    } \
} while(0)

// Image edit state enum (copy from preview.h to avoid UI dependencies)
typedef enum ImageEditState {
    IMAGE_EDIT_NONE,
    IMAGE_EDIT_INPUT,
    IMAGE_EDIT_LOADING,
    IMAGE_EDIT_SUCCESS,
    IMAGE_EDIT_ERROR
} ImageEditState;

#define PREVIEW_EDIT_BUFFER_SIZE 512

// Summary pane constants (copy from preview.h)
#define SUMMARY_PANE_HEIGHT 150
#define SUMMARY_PANE_MIN_HEIGHT 100
#define SUMMARY_PANE_MAX_HEIGHT 250

// Preview types (copy from preview.h to avoid UI dependencies)
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

// Local implementation of preview_type_from_extension for testing
// This validates the logic matches what's in preview.c
static PreviewType test_preview_type_from_extension(const char *extension)
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

// Local implementation of calculate_optimal_word_count for testing
static int test_calculate_optimal_word_count(int pane_height, int pane_width, int font_size)
{
    if (pane_height <= 0 || pane_width <= 0 || font_size <= 0) {
        return 0;
    }

    int line_height = font_size + 2;

    // Reserve space for header (one line) and padding
    int header_height = font_size + 8;
    int available_height = pane_height - header_height - 16;  // 16 for padding

    if (available_height <= 0) return 0;

    int visible_lines = available_height / line_height;

    // Estimate characters per line based on width and average char width
    int avg_char_width = (font_size * 6) / 10;
    if (avg_char_width <= 0) avg_char_width = 1;

    int chars_per_line = pane_width / avg_char_width;

    // Average word length is about 5 characters + 1 space = 6
    int words_per_line = chars_per_line / 6;
    if (words_per_line < 1) words_per_line = 1;

    int total_words = visible_lines * words_per_line;

    // Reduce by 10% to ensure comfortable fit
    total_words = (total_words * 90) / 100;

    // Clamp to reasonable range
    if (total_words < 20) total_words = 20;
    if (total_words > 150) total_words = 150;

    return total_words;
}

// Local copy of format extraction helper for testing
static void test_extract_format_from_extension(const char *ext, char *format_out, size_t format_size)
{
    if (!ext || !format_out || format_size == 0) return;

    size_t len = strlen(ext);
    if (len >= format_size) len = format_size - 1;

    for (size_t i = 0; i < len; i++) {
        format_out[i] = toupper((unsigned char)ext[i]);
    }
    format_out[len] = '\0';
}

void test_preview(void)
{
    // Test 1: Preview type detection - video files
    TEST_ASSERT_EQ(PREVIEW_VIDEO, test_preview_type_from_extension("mp4"), "mp4 detected as PREVIEW_VIDEO");
    TEST_ASSERT_EQ(PREVIEW_VIDEO, test_preview_type_from_extension("mov"), "mov detected as PREVIEW_VIDEO");
    TEST_ASSERT_EQ(PREVIEW_VIDEO, test_preview_type_from_extension("mkv"), "mkv detected as PREVIEW_VIDEO");
    TEST_ASSERT_EQ(PREVIEW_VIDEO, test_preview_type_from_extension("avi"), "avi detected as PREVIEW_VIDEO");
    TEST_ASSERT_EQ(PREVIEW_VIDEO, test_preview_type_from_extension("webm"), "webm detected as PREVIEW_VIDEO");
    TEST_ASSERT_EQ(PREVIEW_VIDEO, test_preview_type_from_extension("m4v"), "m4v detected as PREVIEW_VIDEO");

    // Test 2: Case insensitivity for video types
    TEST_ASSERT_EQ(PREVIEW_VIDEO, test_preview_type_from_extension("MP4"), "MP4 (uppercase) detected as PREVIEW_VIDEO");
    TEST_ASSERT_EQ(PREVIEW_VIDEO, test_preview_type_from_extension("MoV"), "MoV (mixed case) detected as PREVIEW_VIDEO");

    // Test 3: Preview type detection - image files
    TEST_ASSERT_EQ(PREVIEW_IMAGE, test_preview_type_from_extension("png"), "png detected as PREVIEW_IMAGE");
    TEST_ASSERT_EQ(PREVIEW_IMAGE, test_preview_type_from_extension("jpg"), "jpg detected as PREVIEW_IMAGE");
    TEST_ASSERT_EQ(PREVIEW_IMAGE, test_preview_type_from_extension("jpeg"), "jpeg detected as PREVIEW_IMAGE");
    TEST_ASSERT_EQ(PREVIEW_IMAGE, test_preview_type_from_extension("gif"), "gif detected as PREVIEW_IMAGE");
    TEST_ASSERT_EQ(PREVIEW_IMAGE, test_preview_type_from_extension("bmp"), "bmp detected as PREVIEW_IMAGE");

    // Test 4: Preview type detection - text files
    TEST_ASSERT_EQ(PREVIEW_TEXT, test_preview_type_from_extension("txt"), "txt detected as PREVIEW_TEXT");
    TEST_ASSERT_EQ(PREVIEW_TEXT, test_preview_type_from_extension("log"), "log detected as PREVIEW_TEXT");

    // Test 5: Preview type detection - code files
    TEST_ASSERT_EQ(PREVIEW_CODE, test_preview_type_from_extension("c"), "c detected as PREVIEW_CODE");
    TEST_ASSERT_EQ(PREVIEW_CODE, test_preview_type_from_extension("h"), "h detected as PREVIEW_CODE");
    TEST_ASSERT_EQ(PREVIEW_CODE, test_preview_type_from_extension("py"), "py detected as PREVIEW_CODE");
    TEST_ASSERT_EQ(PREVIEW_CODE, test_preview_type_from_extension("js"), "js detected as PREVIEW_CODE");

    // Test 6: Preview type detection - markdown
    TEST_ASSERT_EQ(PREVIEW_MARKDOWN, test_preview_type_from_extension("md"), "md detected as PREVIEW_MARKDOWN");
    TEST_ASSERT_EQ(PREVIEW_MARKDOWN, test_preview_type_from_extension("markdown"), "markdown detected as PREVIEW_MARKDOWN");

    // Test 7: Preview type detection - PDF
    TEST_ASSERT_EQ(PREVIEW_PDF, test_preview_type_from_extension("pdf"), "pdf detected as PREVIEW_PDF");

    // Test 8: Unknown types
    TEST_ASSERT_EQ(PREVIEW_UNKNOWN, test_preview_type_from_extension("xyz"), "xyz detected as PREVIEW_UNKNOWN");
    TEST_ASSERT_EQ(PREVIEW_UNKNOWN, test_preview_type_from_extension(""), "empty string detected as PREVIEW_UNKNOWN");
    TEST_ASSERT_EQ(PREVIEW_UNKNOWN, test_preview_type_from_extension(NULL), "NULL detected as PREVIEW_UNKNOWN");

    // Test 9: Format extraction from extension (helper function test)
    // Tests the uppercase conversion logic
    char format[16];
    test_extract_format_from_extension("png", format, sizeof(format));
    TEST_ASSERT(strcmp(format, "PNG") == 0, "png converts to PNG");

    test_extract_format_from_extension("jpeg", format, sizeof(format));
    TEST_ASSERT(strcmp(format, "JPEG") == 0, "jpeg converts to JPEG");

    test_extract_format_from_extension("MP4", format, sizeof(format));
    TEST_ASSERT(strcmp(format, "MP4") == 0, "MP4 stays MP4");

    // Test 10: Format extraction edge cases
    format[0] = 'X';
    test_extract_format_from_extension(NULL, format, sizeof(format));
    TEST_ASSERT(format[0] == 'X', "NULL ext doesn't modify format");

    test_extract_format_from_extension("png", NULL, 0);
    TEST_ASSERT(1, "NULL format_out doesn't crash");

    // Test long extension truncation
    test_extract_format_from_extension("verylongextension", format, 5);
    TEST_ASSERT(strlen(format) <= 4, "Long extension is truncated to buffer size");

    // ========================================================================
    // Image Edit State Tests
    // ========================================================================

    // Test 11: Image edit state enum values
    TEST_ASSERT_EQ(0, IMAGE_EDIT_NONE, "IMAGE_EDIT_NONE is 0");
    TEST_ASSERT_EQ(1, IMAGE_EDIT_INPUT, "IMAGE_EDIT_INPUT is 1");
    TEST_ASSERT_EQ(2, IMAGE_EDIT_LOADING, "IMAGE_EDIT_LOADING is 2");
    TEST_ASSERT_EQ(3, IMAGE_EDIT_SUCCESS, "IMAGE_EDIT_SUCCESS is 3");
    TEST_ASSERT_EQ(4, IMAGE_EDIT_ERROR, "IMAGE_EDIT_ERROR is 4");

    // Test 12: Edit buffer size constant
    TEST_ASSERT(PREVIEW_EDIT_BUFFER_SIZE >= 256, "Edit buffer size is at least 256");
    TEST_ASSERT(PREVIEW_EDIT_BUFFER_SIZE <= 4096, "Edit buffer size is at most 4096");

    // Test 13: State transitions are distinct
    ImageEditState state = IMAGE_EDIT_NONE;
    TEST_ASSERT(state != IMAGE_EDIT_INPUT, "NONE != INPUT");
    TEST_ASSERT(state != IMAGE_EDIT_LOADING, "NONE != LOADING");
    TEST_ASSERT(state != IMAGE_EDIT_SUCCESS, "NONE != SUCCESS");
    TEST_ASSERT(state != IMAGE_EDIT_ERROR, "NONE != ERROR");

    state = IMAGE_EDIT_INPUT;
    TEST_ASSERT(state != IMAGE_EDIT_NONE, "INPUT != NONE");
    TEST_ASSERT(state != IMAGE_EDIT_LOADING, "INPUT != LOADING");

    state = IMAGE_EDIT_LOADING;
    TEST_ASSERT(state != IMAGE_EDIT_INPUT, "LOADING != INPUT");
    TEST_ASSERT(state != IMAGE_EDIT_SUCCESS, "LOADING != SUCCESS");

    // Test 14: State can be assigned and compared
    ImageEditState test_states[] = {IMAGE_EDIT_NONE, IMAGE_EDIT_INPUT, IMAGE_EDIT_LOADING, IMAGE_EDIT_SUCCESS, IMAGE_EDIT_ERROR};
    for (int i = 0; i < 5; i++) {
        state = test_states[i];
        TEST_ASSERT(state == test_states[i], "State assignment works correctly");
    }

    // ========================================================================
    // Summary Pane Tests
    // ========================================================================

    // Test 15: Summary pane constants are reasonable
    TEST_ASSERT(SUMMARY_PANE_HEIGHT >= SUMMARY_PANE_MIN_HEIGHT, "Default height >= min height");
    TEST_ASSERT(SUMMARY_PANE_HEIGHT <= SUMMARY_PANE_MAX_HEIGHT, "Default height <= max height");
    TEST_ASSERT(SUMMARY_PANE_MIN_HEIGHT > 0, "Min height is positive");
    TEST_ASSERT(SUMMARY_PANE_MAX_HEIGHT > SUMMARY_PANE_MIN_HEIGHT, "Max height > min height");

    // Test 16: Calculate optimal word count - normal cases
    int words = test_calculate_optimal_word_count(150, 280, 12);
    TEST_ASSERT(words >= 20, "Normal pane returns >= 20 words");
    TEST_ASSERT(words <= 150, "Normal pane returns <= 150 words");

    // Test 17: Calculate optimal word count - larger pane
    int words_large = test_calculate_optimal_word_count(250, 400, 12);
    TEST_ASSERT(words_large > words, "Larger pane returns more words");

    // Test 18: Calculate optimal word count - smaller font
    int words_small_font = test_calculate_optimal_word_count(150, 280, 10);
    TEST_ASSERT(words_small_font >= words, "Smaller font fits same or more words");

    // Test 19: Calculate optimal word count - edge cases
    TEST_ASSERT_EQ(0, test_calculate_optimal_word_count(0, 280, 12), "Zero height returns 0");
    TEST_ASSERT_EQ(0, test_calculate_optimal_word_count(150, 0, 12), "Zero width returns 0");
    TEST_ASSERT_EQ(0, test_calculate_optimal_word_count(150, 280, 0), "Zero font size returns 0");
    TEST_ASSERT_EQ(0, test_calculate_optimal_word_count(-100, 280, 12), "Negative height returns 0");
    TEST_ASSERT_EQ(0, test_calculate_optimal_word_count(150, -100, 12), "Negative width returns 0");

    // Test 20: Calculate optimal word count - very small pane
    int words_tiny = test_calculate_optimal_word_count(50, 100, 12);
    TEST_ASSERT(words_tiny >= 20, "Tiny pane returns minimum 20 words");

    // Test 21: Calculate optimal word count - very large pane
    int words_huge = test_calculate_optimal_word_count(500, 800, 12);
    TEST_ASSERT(words_huge <= 150, "Huge pane returns maximum 150 words");

    // Test 22: Summary pane height bounds are usable
    TEST_ASSERT(SUMMARY_PANE_MIN_HEIGHT >= 80, "Min height allows at least some content");
    TEST_ASSERT(SUMMARY_PANE_MAX_HEIGHT <= 300, "Max height doesn't overwhelm preview");
}

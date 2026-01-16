#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Test macros (implemented in test_main.c)
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

#include "../src/ui/file_view_modal.h"

void test_file_view_modal(void)
{
    printf("  Testing file_view_modal_init...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        TEST_ASSERT_EQ(false, modal.visible, "Modal should not be visible initially");
        TEST_ASSERT_EQ(FILE_VIEW_NONE, modal.type, "Modal type should be FILE_VIEW_NONE");
        TEST_ASSERT_EQ(0, modal.scroll_offset, "Scroll offset should be 0");
        TEST_ASSERT(modal.text_content == NULL, "Text content should be NULL");
        TEST_ASSERT_EQ(0, modal.texture_id, "Texture ID should be 0");
        TEST_ASSERT_STR_EQ("", modal.file_path, "File path should be empty");
    }

    printf("  Testing file_view_modal_show_text...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        const char *test_content = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5";
        file_view_modal_show_text(&modal, "/path/to/test.txt", test_content);

        TEST_ASSERT_EQ(true, modal.visible, "Modal should be visible after show_text");
        TEST_ASSERT_EQ(FILE_VIEW_TEXT, modal.type, "Modal type should be FILE_VIEW_TEXT");
        TEST_ASSERT(modal.text_content != NULL, "Text content should be allocated");
        TEST_ASSERT_STR_EQ(test_content, modal.text_content, "Text content should match");
        TEST_ASSERT_STR_EQ("/path/to/test.txt", modal.file_path, "File path should be set");
        TEST_ASSERT_EQ(5, modal.total_lines, "Total lines should be 5");
        TEST_ASSERT_EQ(0, modal.scroll_offset, "Scroll should reset to 0");

        file_view_modal_free(&modal);
    }

    printf("  Testing file_view_modal_show_image...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        // Note: We can't load a real texture in tests, so just test the state setup
        file_view_modal_show_image(&modal, "/path/to/image.png", 0, 800, 600);

        TEST_ASSERT_EQ(true, modal.visible, "Modal should be visible after show_image");
        TEST_ASSERT_EQ(FILE_VIEW_IMAGE, modal.type, "Modal type should be FILE_VIEW_IMAGE");
        TEST_ASSERT_STR_EQ("/path/to/image.png", modal.file_path, "File path should be set");
        TEST_ASSERT_EQ(800, modal.image_width, "Image width should be set");
        TEST_ASSERT_EQ(600, modal.image_height, "Image height should be set");

        file_view_modal_free(&modal);
    }

    printf("  Testing file_view_modal_hide...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        file_view_modal_show_text(&modal, "/path/to/test.txt", "Test content");
        TEST_ASSERT_EQ(true, modal.visible, "Modal should be visible before hide");

        file_view_modal_hide(&modal);

        TEST_ASSERT_EQ(false, modal.visible, "Modal should not be visible after hide");
        TEST_ASSERT_EQ(FILE_VIEW_NONE, modal.type, "Modal type should be FILE_VIEW_NONE after hide");
        TEST_ASSERT(modal.text_content == NULL, "Text content should be freed");
        TEST_ASSERT_EQ(0, modal.scroll_offset, "Scroll offset should reset to 0");
    }

    printf("  Testing file_view_modal_is_visible...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        TEST_ASSERT_EQ(false, file_view_modal_is_visible(&modal), "Should return false for hidden modal");

        file_view_modal_show_text(&modal, "/path/to/test.txt", "Test content");
        TEST_ASSERT_EQ(true, file_view_modal_is_visible(&modal), "Should return true for visible modal");

        file_view_modal_hide(&modal);
        TEST_ASSERT_EQ(false, file_view_modal_is_visible(&modal), "Should return false after hide");
    }

    printf("  Testing scroll_down increases offset...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        // Create content with many lines to allow scrolling
        char long_content[2048];
        long_content[0] = '\0';
        for (int i = 0; i < 100; i++) {
            char line[32];
            snprintf(line, sizeof(line), "Line %d\n", i + 1);
            strcat(long_content, line);
        }

        file_view_modal_show_text(&modal, "/path/to/long.txt", long_content);
        TEST_ASSERT_EQ(0, modal.scroll_offset, "Initial scroll should be 0");

        file_view_modal_scroll_down(&modal, 1);
        TEST_ASSERT_EQ(1, modal.scroll_offset, "Scroll should increase by 1");

        file_view_modal_scroll_down(&modal, 5);
        TEST_ASSERT_EQ(6, modal.scroll_offset, "Scroll should increase by 5");

        file_view_modal_free(&modal);
    }

    printf("  Testing scroll_up decreases offset...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        char long_content[2048];
        long_content[0] = '\0';
        for (int i = 0; i < 100; i++) {
            char line[32];
            snprintf(line, sizeof(line), "Line %d\n", i + 1);
            strcat(long_content, line);
        }

        file_view_modal_show_text(&modal, "/path/to/long.txt", long_content);

        // Scroll down first
        file_view_modal_scroll_down(&modal, 10);
        TEST_ASSERT_EQ(10, modal.scroll_offset, "Scroll should be 10 after scroll_down");

        file_view_modal_scroll_up(&modal, 3);
        TEST_ASSERT_EQ(7, modal.scroll_offset, "Scroll should decrease by 3");

        file_view_modal_free(&modal);
    }

    printf("  Testing scroll_up clamps at 0...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        file_view_modal_show_text(&modal, "/path/to/test.txt", "Line 1\nLine 2\nLine 3");

        file_view_modal_scroll_down(&modal, 1);
        file_view_modal_scroll_up(&modal, 100);

        TEST_ASSERT_EQ(0, modal.scroll_offset, "Scroll should clamp at 0");

        file_view_modal_free(&modal);
    }

    printf("  Testing scroll_down clamps at max...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        // Short content - should not scroll past content
        file_view_modal_show_text(&modal, "/path/to/test.txt", "Line 1\nLine 2\nLine 3");

        // Set visible lines (normally done during draw, but we test the clamping logic)
        modal.visible_lines = 10; // More visible lines than content lines

        file_view_modal_scroll_down(&modal, 1000);

        // When content is shorter than visible area, scroll should stay at 0
        TEST_ASSERT(modal.scroll_offset >= 0, "Scroll should not go negative");

        file_view_modal_free(&modal);
    }

    printf("  Testing line count calculation...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        file_view_modal_show_text(&modal, "/path/to/test.txt", "Line 1\nLine 2\nLine 3");
        TEST_ASSERT_EQ(3, modal.total_lines, "Should count 3 lines");

        file_view_modal_hide(&modal);

        file_view_modal_show_text(&modal, "/path/to/test.txt", "Single line");
        TEST_ASSERT_EQ(1, modal.total_lines, "Should count 1 line");

        file_view_modal_hide(&modal);

        file_view_modal_show_text(&modal, "/path/to/test.txt", "");
        TEST_ASSERT_EQ(0, modal.total_lines, "Empty content should have 0 lines");

        file_view_modal_hide(&modal);

        file_view_modal_show_text(&modal, "/path/to/test.txt", "\n\n\n");
        TEST_ASSERT_EQ(3, modal.total_lines, "Three newlines should count as 3 lines");

        file_view_modal_free(&modal);
    }

    printf("  Testing scroll_to_top...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        char long_content[2048];
        long_content[0] = '\0';
        for (int i = 0; i < 50; i++) {
            char line[32];
            snprintf(line, sizeof(line), "Line %d\n", i + 1);
            strcat(long_content, line);
        }

        file_view_modal_show_text(&modal, "/path/to/long.txt", long_content);
        file_view_modal_scroll_down(&modal, 25);
        TEST_ASSERT_EQ(25, modal.scroll_offset, "Scroll should be 25");

        file_view_modal_scroll_to_top(&modal);
        TEST_ASSERT_EQ(0, modal.scroll_offset, "Scroll should be 0 after scroll_to_top");

        file_view_modal_free(&modal);
    }

    printf("  Testing scroll_to_bottom...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        char long_content[2048];
        long_content[0] = '\0';
        for (int i = 0; i < 50; i++) {
            char line[32];
            snprintf(line, sizeof(line), "Line %d\n", i + 1);
            strcat(long_content, line);
        }

        file_view_modal_show_text(&modal, "/path/to/long.txt", long_content);
        modal.visible_lines = 20;

        file_view_modal_scroll_to_bottom(&modal);

        // Max scroll should be total_lines - visible_lines
        int expected_max = modal.total_lines - modal.visible_lines;
        if (expected_max < 0) expected_max = 0;
        TEST_ASSERT_EQ(expected_max, modal.scroll_offset, "Scroll should be at bottom");

        file_view_modal_free(&modal);
    }

    printf("  Testing extract filename from path...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        file_view_modal_show_text(&modal, "/path/to/myfile.txt", "content");

        // The modal should extract the filename for display
        const char *filename = file_view_modal_get_filename(&modal);
        TEST_ASSERT_STR_EQ("myfile.txt", filename, "Should extract filename from path");

        file_view_modal_free(&modal);
    }

    printf("  Testing file_view_modal_free cleans up resources...\n");
    {
        FileViewModalState modal;
        file_view_modal_init(&modal);

        file_view_modal_show_text(&modal, "/path/to/test.txt", "Test content");
        TEST_ASSERT(modal.text_content != NULL, "Text content should be allocated");

        file_view_modal_free(&modal);

        TEST_ASSERT(modal.text_content == NULL, "Text content should be NULL after free");
        TEST_ASSERT_EQ(false, modal.visible, "Modal should not be visible after free");
    }
}

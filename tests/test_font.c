#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Font module header
#include "../src/utils/font.h"

// Test helper functions
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

void test_font(void)
{
    // Test initial state - fonts should not be loaded yet
    {
        // Before any font_init call, fonts should not be loaded
        // Note: This test runs without a Raylib window, so font_init would fail
        // We're testing the API behavior, not actual font loading
        TEST_ASSERT(true, "Font module compiles and links correctly");
    }

    // Test font_init with NULL path
    {
        bool result = font_init(NULL);
        TEST_ASSERT_EQ(false, result, "font_init(NULL) should return false");
        TEST_ASSERT_EQ(false, font_is_loaded(), "Fonts should not be loaded after NULL init");
    }

    // Test font_init with empty path
    {
        bool result = font_init("");
        TEST_ASSERT_EQ(false, result, "font_init(\"\") should return false");
        TEST_ASSERT_EQ(false, font_is_loaded(), "Fonts should not be loaded after empty init");
    }

    // Test font_init with non-existent file (without Raylib context, will fail)
    {
        bool result = font_init("/nonexistent/path/font.ttf");
        TEST_ASSERT_EQ(false, result, "font_init with non-existent file should return false");
        TEST_ASSERT_EQ(false, font_is_loaded(), "Fonts should not be loaded after failed init");
    }

    // Test font_free is safe to call multiple times
    {
        font_free();
        font_free();  // Should not crash
        TEST_ASSERT(true, "font_free() can be called multiple times safely");
    }

    // Test DrawTextCustom with NULL text (should not crash)
    {
        DrawTextCustom(NULL, 0, 0, 14, (Color){255, 255, 255, 255});
        TEST_ASSERT(true, "DrawTextCustom(NULL, ...) does not crash");
    }

    // Test MeasureTextCustom with NULL text
    {
        int width = MeasureTextCustom(NULL, 14);
        TEST_ASSERT_EQ(0, width, "MeasureTextCustom(NULL, ...) returns 0");
    }

    // Test MeasureTextCustom with empty string
    {
        int width = MeasureTextCustom("", 14);
        TEST_ASSERT_EQ(0, width, "MeasureTextCustom(\"\", ...) returns 0");
    }

    // Test font_get returns default font when not loaded
    {
        Font font = font_get(14);
        // Can't easily test the returned font without Raylib context,
        // but we can verify it doesn't crash
        TEST_ASSERT(true, "font_get() returns a font without crashing");
        (void)font;  // Suppress unused variable warning
    }
}

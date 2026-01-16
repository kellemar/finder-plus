// Test file for progress indicator component
// TDD: These tests should fail first, then pass after implementation

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

// Test helper functions (from test_main.c)
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

#define TEST_ASSERT_FLOAT_EQ(expected, actual, epsilon, message) do { \
    inc_tests_run(); \
    if (fabs((expected) - (actual)) < (epsilon)) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s - expected %.4f, got %.4f (line %d)\n", message, (float)(expected), (float)(actual), __LINE__); \
    } \
} while(0)

// Include the header we're testing
#include "../src/ui/progress_indicator.h"

// Test: Initialization with PROGRESS_SPINNER type
static void test_init_spinner(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_SPINNER);

    TEST_ASSERT_EQ(PROGRESS_SPINNER, pi.type, "Type should be PROGRESS_SPINNER");
    TEST_ASSERT_FLOAT_EQ(0.0f, pi.animation_time, 0.001f, "Animation time should start at 0");
    TEST_ASSERT_EQ(0, pi.progress, "Progress should start at 0");
    TEST_ASSERT_EQ(false, pi.visible, "Should not be visible initially");
    TEST_ASSERT_STR_EQ("", pi.message, "Message should be empty initially");
}

// Test: Initialization with PROGRESS_DOTS type
static void test_init_dots(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_DOTS);

    TEST_ASSERT_EQ(PROGRESS_DOTS, pi.type, "Type should be PROGRESS_DOTS");
    TEST_ASSERT_FLOAT_EQ(0.0f, pi.animation_time, 0.001f, "Animation time should start at 0");
}

// Test: Initialization with PROGRESS_BAR type
static void test_init_bar(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_BAR);

    TEST_ASSERT_EQ(PROGRESS_BAR, pi.type, "Type should be PROGRESS_BAR");
    TEST_ASSERT_EQ(0, pi.progress, "Progress should start at 0");
}

// Test: Update function accumulates time
static void test_update_accumulates_time(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_SPINNER);

    progress_indicator_update(&pi, 0.016f);  // ~60fps frame
    TEST_ASSERT_FLOAT_EQ(0.016f, pi.animation_time, 0.001f, "Animation time should accumulate");

    progress_indicator_update(&pi, 0.016f);
    TEST_ASSERT_FLOAT_EQ(0.032f, pi.animation_time, 0.001f, "Animation time should continue accumulating");
}

// Test: Update function wraps time for spinner (full rotation)
static void test_update_wraps_time(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_SPINNER);

    // Simulate many frames to exceed 1 second (full rotation)
    for (int i = 0; i < 100; i++) {
        progress_indicator_update(&pi, 0.016f);
    }

    // Time should wrap around (stay within reasonable bounds)
    TEST_ASSERT(pi.animation_time < 10.0f, "Animation time should wrap or stay bounded");
}

// Test: Set message function
static void test_set_message(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_SPINNER);

    progress_indicator_set_message(&pi, "Loading...");
    TEST_ASSERT_STR_EQ("Loading...", pi.message, "Message should be set correctly");

    progress_indicator_set_message(&pi, "Processing");
    TEST_ASSERT_STR_EQ("Processing", pi.message, "Message should update");
}

// Test: Set message with NULL
static void test_set_message_null(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_SPINNER);

    progress_indicator_set_message(&pi, "Test");
    progress_indicator_set_message(&pi, NULL);
    TEST_ASSERT_STR_EQ("", pi.message, "NULL message should clear to empty string");
}

// Test: Set message truncation for long strings
static void test_set_message_truncation(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_SPINNER);

    // Create a very long message (longer than 128 char buffer)
    char long_msg[256];
    memset(long_msg, 'X', sizeof(long_msg) - 1);
    long_msg[255] = '\0';

    progress_indicator_set_message(&pi, long_msg);

    // Should be truncated, not overflow
    TEST_ASSERT(strlen(pi.message) < 128, "Long message should be truncated");
    TEST_ASSERT(strlen(pi.message) > 0, "Truncated message should not be empty");
}

// Test: Progress clamping via set_progress function
static void test_progress_clamping(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_BAR);

    // Test normal value
    progress_indicator_set_progress(&pi, 50);
    TEST_ASSERT_EQ(50, pi.progress, "Progress should be 50");

    // Test clamping over 100
    progress_indicator_set_progress(&pi, 150);
    TEST_ASSERT_EQ(100, pi.progress, "Progress should clamp to 100");

    // Test clamping under 0
    progress_indicator_set_progress(&pi, -10);
    TEST_ASSERT_EQ(0, pi.progress, "Progress should clamp to 0");

    // Test boundary values
    progress_indicator_set_progress(&pi, 0);
    TEST_ASSERT_EQ(0, pi.progress, "Progress at 0 should stay 0");

    progress_indicator_set_progress(&pi, 100);
    TEST_ASSERT_EQ(100, pi.progress, "Progress at 100 should stay 100");
}

// Test: Visible state
static void test_visible_state(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_SPINNER);

    TEST_ASSERT_EQ(false, pi.visible, "Should start not visible");

    pi.visible = true;
    TEST_ASSERT_EQ(true, pi.visible, "Should be visible after setting");

    pi.visible = false;
    TEST_ASSERT_EQ(false, pi.visible, "Should be not visible after clearing");
}

// Test: Dots animation cycle (0-3 dots)
static void test_dots_animation_cycle(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_DOTS);

    // At time 0, should have 0 dots (or 1, depending on impl)
    TEST_ASSERT_FLOAT_EQ(0.0f, pi.animation_time, 0.001f, "Should start at 0");

    // After some time, animation should progress
    progress_indicator_update(&pi, 0.5f);
    TEST_ASSERT(pi.animation_time > 0.0f, "Animation time should increase");
}

// Test: Spinner rotation calculation
static void test_spinner_rotation(void)
{
    ProgressIndicator pi;
    progress_indicator_init(&pi, PROGRESS_SPINNER);

    // At time 0, angle should be 0
    float angle = pi.animation_time * 360.0f;
    TEST_ASSERT_FLOAT_EQ(0.0f, angle, 0.001f, "Angle should start at 0");

    // After 0.5 seconds, angle should be 180 degrees
    progress_indicator_update(&pi, 0.5f);
    angle = pi.animation_time * 360.0f;
    TEST_ASSERT_FLOAT_EQ(180.0f, angle, 0.1f, "Angle should be 180 after 0.5s");

    // After 1 second total, angle should be 360 (full rotation)
    progress_indicator_update(&pi, 0.5f);
    angle = pi.animation_time * 360.0f;
    TEST_ASSERT_FLOAT_EQ(360.0f, angle, 0.1f, "Angle should be 360 after 1s");
}

// Test: Multiple types don't interfere
static void test_multiple_instances(void)
{
    ProgressIndicator spinner, dots, bar;

    progress_indicator_init(&spinner, PROGRESS_SPINNER);
    progress_indicator_init(&dots, PROGRESS_DOTS);
    progress_indicator_init(&bar, PROGRESS_BAR);

    progress_indicator_set_message(&spinner, "Spinner");
    progress_indicator_set_message(&dots, "Dots");
    progress_indicator_set_message(&bar, "Bar");

    TEST_ASSERT_STR_EQ("Spinner", spinner.message, "Spinner message should be independent");
    TEST_ASSERT_STR_EQ("Dots", dots.message, "Dots message should be independent");
    TEST_ASSERT_STR_EQ("Bar", bar.message, "Bar message should be independent");

    TEST_ASSERT_EQ(PROGRESS_SPINNER, spinner.type, "Spinner type preserved");
    TEST_ASSERT_EQ(PROGRESS_DOTS, dots.type, "Dots type preserved");
    TEST_ASSERT_EQ(PROGRESS_BAR, bar.type, "Bar type preserved");
}

// Main test function called from test_main.c
void test_progress_indicator(void)
{
    printf("  [Initialization Tests]\n");
    test_init_spinner();
    test_init_dots();
    test_init_bar();

    printf("  [Update Tests]\n");
    test_update_accumulates_time();
    test_update_wraps_time();

    printf("  [Message Tests]\n");
    test_set_message();
    test_set_message_null();
    test_set_message_truncation();

    printf("  [State Tests]\n");
    test_progress_clamping();
    test_visible_state();

    printf("  [Animation Tests]\n");
    test_dots_animation_cycle();
    test_spinner_rotation();

    printf("  [Independence Tests]\n");
    test_multiple_instances();
}

#include <stdio.h>
#include <string.h>

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

#include "../src/ui/dialog.h"

static void dummy_callback(struct App *app)
{
    (void)app;
}

void test_dialog(void)
{
    printf("  Testing dialog_init...\n");
    {
        DialogState dialog;
        dialog_init(&dialog);

        TEST_ASSERT_EQ(false, dialog.visible, "Dialog should not be visible initially");
        TEST_ASSERT_EQ(DIALOG_NONE, dialog.type, "Dialog type should be DIALOG_NONE");
        TEST_ASSERT_EQ(DIALOG_BTN_CANCEL, dialog.selected, "Default button should be CANCEL");
        TEST_ASSERT(dialog.on_confirm == NULL, "Callback should be NULL");
        TEST_ASSERT(dialog.user_data == NULL, "User data should be NULL");
    }

    printf("  Testing dialog_confirm...\n");
    {
        DialogState dialog;
        dialog_init(&dialog);

        dialog_confirm(&dialog, "Delete File", "Are you sure you want to delete test.txt?", dummy_callback);

        TEST_ASSERT_EQ(true, dialog.visible, "Dialog should be visible");
        TEST_ASSERT_EQ(DIALOG_CONFIRM, dialog.type, "Dialog type should be DIALOG_CONFIRM");
        TEST_ASSERT_STR_EQ("Delete File", dialog.title, "Title should be set");
        TEST_ASSERT_STR_EQ("Are you sure you want to delete test.txt?", dialog.message, "Message should be set");
        TEST_ASSERT_EQ(DIALOG_BTN_CANCEL, dialog.selected, "Default button should be CANCEL for safety");
        TEST_ASSERT(dialog.on_confirm == dummy_callback, "Callback should be set");
    }

    printf("  Testing dialog_error...\n");
    {
        DialogState dialog;
        dialog_init(&dialog);

        dialog_error(&dialog, "Error", "File not found");

        TEST_ASSERT_EQ(true, dialog.visible, "Dialog should be visible");
        TEST_ASSERT_EQ(DIALOG_ERROR, dialog.type, "Dialog type should be DIALOG_ERROR");
        TEST_ASSERT_STR_EQ("Error", dialog.title, "Title should be set");
        TEST_ASSERT_STR_EQ("File not found", dialog.message, "Message should be set");
        TEST_ASSERT_EQ(DIALOG_BTN_OK, dialog.selected, "Default button should be OK for error dialog");
        TEST_ASSERT(dialog.on_confirm == NULL, "Callback should be NULL for error dialog");
    }

    printf("  Testing dialog_info...\n");
    {
        DialogState dialog;
        dialog_init(&dialog);

        dialog_info(&dialog, "Information", "Operation completed successfully");

        TEST_ASSERT_EQ(true, dialog.visible, "Dialog should be visible");
        TEST_ASSERT_EQ(DIALOG_INFO, dialog.type, "Dialog type should be DIALOG_INFO");
        TEST_ASSERT_STR_EQ("Information", dialog.title, "Title should be set");
        TEST_ASSERT_STR_EQ("Operation completed successfully", dialog.message, "Message should be set");
        TEST_ASSERT_EQ(DIALOG_BTN_OK, dialog.selected, "Default button should be OK for info dialog");
        TEST_ASSERT(dialog.on_confirm == NULL, "Callback should be NULL for info dialog");
    }

    printf("  Testing dialog_hide...\n");
    {
        DialogState dialog;
        dialog_init(&dialog);

        dialog_confirm(&dialog, "Test", "Test message", dummy_callback);
        TEST_ASSERT_EQ(true, dialog.visible, "Dialog should be visible before hide");

        dialog_hide(&dialog);

        TEST_ASSERT_EQ(false, dialog.visible, "Dialog should not be visible after hide");
        TEST_ASSERT_EQ(DIALOG_NONE, dialog.type, "Dialog type should be DIALOG_NONE after hide");
        TEST_ASSERT(dialog.on_confirm == NULL, "Callback should be NULL after hide");
        TEST_ASSERT(dialog.user_data == NULL, "User data should be NULL after hide");
    }

    printf("  Testing dialog_is_visible...\n");
    {
        DialogState dialog;
        dialog_init(&dialog);

        TEST_ASSERT_EQ(false, dialog_is_visible(&dialog), "Should return false for hidden dialog");

        dialog_info(&dialog, "Test", "Test");
        TEST_ASSERT_EQ(true, dialog_is_visible(&dialog), "Should return true for visible dialog");

        dialog_hide(&dialog);
        TEST_ASSERT_EQ(false, dialog_is_visible(&dialog), "Should return false after hide");
    }

    printf("  Testing button selection toggle...\n");
    {
        DialogState dialog;
        dialog_init(&dialog);

        dialog_confirm(&dialog, "Test", "Test message", dummy_callback);

        TEST_ASSERT_EQ(DIALOG_BTN_CANCEL, dialog.selected, "Initial selection should be CANCEL");

        dialog.selected = (dialog.selected == DIALOG_BTN_OK) ? DIALOG_BTN_CANCEL : DIALOG_BTN_OK;
        TEST_ASSERT_EQ(DIALOG_BTN_OK, dialog.selected, "Selection should toggle to OK");

        dialog.selected = (dialog.selected == DIALOG_BTN_OK) ? DIALOG_BTN_CANCEL : DIALOG_BTN_OK;
        TEST_ASSERT_EQ(DIALOG_BTN_CANCEL, dialog.selected, "Selection should toggle back to CANCEL");
    }

    printf("  Testing title/message truncation...\n");
    {
        DialogState dialog;
        dialog_init(&dialog);

        char long_title[128];
        memset(long_title, 'A', sizeof(long_title) - 1);
        long_title[sizeof(long_title) - 1] = '\0';

        char long_message[1024];
        memset(long_message, 'B', sizeof(long_message) - 1);
        long_message[sizeof(long_message) - 1] = '\0';

        dialog_info(&dialog, long_title, long_message);

        TEST_ASSERT(strlen(dialog.title) < sizeof(dialog.title), "Title should be truncated");
        TEST_ASSERT(strlen(dialog.message) < sizeof(dialog.message), "Message should be truncated");
        TEST_ASSERT(dialog.title[0] == 'A', "Title should start with A");
        TEST_ASSERT(dialog.message[0] == 'B', "Message should start with B");
    }
}

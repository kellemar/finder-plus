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

#include "../src/ui/context_menu.h"

void test_context_menu(void)
{
    printf("  Testing context_menu_init...\n");
    {
        ContextMenuState menu;
        context_menu_init(&menu);

        TEST_ASSERT_EQ(false, menu.visible, "Menu should not be visible initially");
        TEST_ASSERT_EQ(CONTEXT_NONE, menu.type, "Menu type should be CONTEXT_NONE");
        TEST_ASSERT_EQ(-1, menu.target_index, "Target index should be -1");
        TEST_ASSERT_EQ(-1, menu.hovered_index, "Hovered index should be -1");
        TEST_ASSERT_EQ(0, menu.item_count, "Item count should be 0");
        TEST_ASSERT_EQ('\0', menu.target_path[0], "Target path should be empty");
    }

    printf("  Testing context_menu_is_visible...\n");
    {
        ContextMenuState menu;
        context_menu_init(&menu);

        TEST_ASSERT_EQ(false, context_menu_is_visible(&menu), "Should return false for hidden menu");

        menu.visible = true;
        TEST_ASSERT_EQ(true, context_menu_is_visible(&menu), "Should return true for visible menu");

        menu.visible = false;
        TEST_ASSERT_EQ(false, context_menu_is_visible(&menu), "Should return false after setting visible to false");
    }

    printf("  Testing context_menu_hide...\n");
    {
        ContextMenuState menu;
        context_menu_init(&menu);

        // Simulate menu being shown
        menu.visible = true;
        menu.type = CONTEXT_FILE;
        menu.item_count = 5;
        menu.target_index = 3;
        menu.hovered_index = 2;
        menu.selected_index = 1;
        strncpy(menu.target_path, "/test/path", sizeof(menu.target_path));

        TEST_ASSERT_EQ(true, menu.visible, "Menu should be visible before hide");

        context_menu_hide(&menu);

        TEST_ASSERT_EQ(false, menu.visible, "Menu should not be visible after hide");
        TEST_ASSERT_EQ(CONTEXT_NONE, menu.type, "Menu type should be CONTEXT_NONE after hide");
        TEST_ASSERT_EQ(0, menu.item_count, "Item count should be 0 after hide");
        TEST_ASSERT_EQ(-1, menu.target_index, "Target index should be -1 after hide");
        TEST_ASSERT_EQ(-1, menu.hovered_index, "Hovered index should be -1 after hide");
        TEST_ASSERT_EQ(-1, menu.selected_index, "Selected index should be -1 after hide");
        TEST_ASSERT_EQ('\0', menu.target_path[0], "Target path should be empty after hide");
    }

    printf("  Testing ContextMenuType enum values...\n");
    {
        TEST_ASSERT_EQ(0, CONTEXT_NONE, "CONTEXT_NONE should be 0");
        TEST_ASSERT_EQ(1, CONTEXT_FILE, "CONTEXT_FILE should be 1");
        TEST_ASSERT_EQ(2, CONTEXT_FOLDER, "CONTEXT_FOLDER should be 2");
        TEST_ASSERT_EQ(3, CONTEXT_EMPTY_SPACE, "CONTEXT_EMPTY_SPACE should be 3");
        TEST_ASSERT_EQ(4, CONTEXT_MULTI_SELECT, "CONTEXT_MULTI_SELECT should be 4");
    }

    printf("  Testing ContextMenuItem structure...\n");
    {
        ContextMenuItem item;
        memset(&item, 0, sizeof(item));

        strncpy(item.label, "Test Label", sizeof(item.label) - 1);
        strncpy(item.shortcut, "Cmd+T", sizeof(item.shortcut) - 1);
        item.enabled = true;
        item.separator_after = false;
        item.action = NULL;

        TEST_ASSERT_STR_EQ("Test Label", item.label, "Item label should match");
        TEST_ASSERT_STR_EQ("Cmd+T", item.shortcut, "Item shortcut should match");
        TEST_ASSERT_EQ(true, item.enabled, "Item should be enabled");
        TEST_ASSERT_EQ(false, item.separator_after, "Item should not have separator after");
        TEST_ASSERT(item.action == NULL, "Item action should be NULL");
    }

    printf("  Testing menu state after multiple init calls...\n");
    {
        ContextMenuState menu;

        // First init
        context_menu_init(&menu);
        menu.visible = true;
        menu.type = CONTEXT_FILE;
        menu.item_count = 10;

        // Second init should reset
        context_menu_init(&menu);

        TEST_ASSERT_EQ(false, menu.visible, "Menu should not be visible after re-init");
        TEST_ASSERT_EQ(CONTEXT_NONE, menu.type, "Menu type should be CONTEXT_NONE after re-init");
        TEST_ASSERT_EQ(0, menu.item_count, "Item count should be 0 after re-init");
    }

    printf("  Testing menu item capacity...\n");
    {
        ContextMenuState menu;
        context_menu_init(&menu);

        TEST_ASSERT(CONTEXT_MENU_MAX_ITEMS >= 8, "Menu should support at least 8 items");
        TEST_ASSERT(CONTEXT_MENU_MAX_ITEMS <= 32, "Menu should have reasonable max items limit");
    }

    printf("  Testing target path buffer size...\n");
    {
        ContextMenuState menu;
        context_menu_init(&menu);

        // Verify the buffer is large enough for typical paths
        TEST_ASSERT(sizeof(menu.target_path) >= 1024, "Target path buffer should be at least 1024 bytes");
    }

    printf("  Testing label and shortcut buffer sizes...\n");
    {
        ContextMenuItem item;

        // Verify buffers are large enough for typical content
        TEST_ASSERT(sizeof(item.label) >= 32, "Label buffer should be at least 32 bytes");
        TEST_ASSERT(sizeof(item.shortcut) >= 12, "Shortcut buffer should be at least 12 bytes");
    }
}

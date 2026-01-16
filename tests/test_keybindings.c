#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "utils/keybindings.h"

// External test macros from test_main.c
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

// Raylib KEY_* constants for testing (duplicated here to avoid raylib dependency)
#define KEY_A 65
#define KEY_C 67
#define KEY_D 68
#define KEY_K 75
#define KEY_N 78
#define KEY_P 80
#define KEY_T 84
#define KEY_V 86
#define KEY_X 88
#define KEY_ONE 49
#define KEY_TWO 50
#define KEY_THREE 51
#define KEY_ENTER 257
#define KEY_BACKSPACE 259
#define KEY_TAB 258
#define KEY_ESCAPE 256
#define KEY_LEFT_BRACKET 91
#define KEY_RIGHT_BRACKET 93

// Test keybindings initialization
static void test_keybindings_init(void)
{
    printf("  Testing keybindings_init...\n");

    KeyBindingConfig config;
    keybindings_init(&config);

    TEST_ASSERT(config.count > 0, "Should have default bindings");
    TEST_ASSERT(!config.modified, "Should not be modified initially");

    // Check some known default bindings
    const KeyBinding *copy_binding = keybindings_get(&config, ACTION_COPY);
    TEST_ASSERT(copy_binding != NULL, "Copy action should have binding");
    TEST_ASSERT_EQ(KEY_C, copy_binding->key, "Copy should be C key");
    TEST_ASSERT(copy_binding->modifiers & MOD_SUPER, "Copy should have Cmd modifier");
    TEST_ASSERT(copy_binding->is_default, "Copy should be marked as default");

    const KeyBinding *cut_binding = keybindings_get(&config, ACTION_CUT);
    TEST_ASSERT(cut_binding != NULL, "Cut action should have binding");
    TEST_ASSERT_EQ(KEY_X, cut_binding->key, "Cut should be X key");

    const KeyBinding *paste_binding = keybindings_get(&config, ACTION_PASTE);
    TEST_ASSERT(paste_binding != NULL, "Paste action should have binding");
    TEST_ASSERT_EQ(KEY_V, paste_binding->key, "Paste should be V key");

    keybindings_free(&config);
}

// Test keybinding set and get
static void test_keybindings_set_get(void)
{
    printf("  Testing keybindings_set and keybindings_get...\n");

    KeyBindingConfig config;
    keybindings_init(&config);

    // Override an existing binding
    bool result = keybindings_set(&config, ACTION_COPY, KEY_D, MOD_SUPER | MOD_SHIFT);
    TEST_ASSERT(result, "Setting binding should succeed");
    TEST_ASSERT(config.modified, "Config should be marked modified");

    const KeyBinding *binding = keybindings_get(&config, ACTION_COPY);
    TEST_ASSERT(binding != NULL, "Should get the binding back");
    TEST_ASSERT_EQ(KEY_D, binding->key, "Key should be updated");
    TEST_ASSERT_EQ(MOD_SUPER | MOD_SHIFT, binding->modifiers, "Modifiers should be updated");
    TEST_ASSERT(!binding->is_default, "Modified binding should not be default");

    // Get non-existent action
    const KeyBinding *none = keybindings_get(&config, ACTION_NONE);
    TEST_ASSERT(none == NULL, "ACTION_NONE should not have binding");

    keybindings_free(&config);
}

// Test keybinding removal
static void test_keybindings_remove(void)
{
    printf("  Testing keybindings_remove...\n");

    KeyBindingConfig config;
    keybindings_init(&config);

    // Verify binding exists
    TEST_ASSERT(keybindings_get(&config, ACTION_COPY) != NULL, "Copy should exist");

    // Remove it
    bool result = keybindings_remove(&config, ACTION_COPY);
    TEST_ASSERT(result, "Remove should succeed");
    TEST_ASSERT(keybindings_get(&config, ACTION_COPY) == NULL, "Copy should not exist after remove");
    TEST_ASSERT(config.modified, "Config should be marked modified");

    // Try to remove again
    result = keybindings_remove(&config, ACTION_COPY);
    TEST_ASSERT(!result, "Remove should fail on non-existent binding");

    keybindings_free(&config);
}

// Test reset to defaults
static void test_keybindings_reset_defaults(void)
{
    printf("  Testing keybindings_reset_defaults...\n");

    KeyBindingConfig config;
    keybindings_init(&config);

    // Modify a binding
    keybindings_set(&config, ACTION_COPY, KEY_D, MOD_SUPER);
    TEST_ASSERT(config.modified, "Config should be modified");

    // Reset
    keybindings_reset_defaults(&config);
    TEST_ASSERT(!config.modified, "Config should not be modified after reset");

    // Verify copy is back to default
    const KeyBinding *copy = keybindings_get(&config, ACTION_COPY);
    TEST_ASSERT(copy != NULL, "Copy should exist");
    TEST_ASSERT_EQ(KEY_C, copy->key, "Copy should be C key again");
    TEST_ASSERT(copy->is_default, "Copy should be marked as default");

    keybindings_free(&config);
}

// Test conflict detection
static void test_keybindings_conflict(void)
{
    printf("  Testing keybindings_find_conflict...\n");

    KeyBindingConfig config;
    keybindings_init(&config);

    // Check for conflict with existing Cmd+C (copy)
    KeyAction conflict = keybindings_find_conflict(&config, KEY_C, MOD_SUPER, ACTION_NONE);
    TEST_ASSERT_EQ(ACTION_COPY, conflict, "Should detect conflict with copy");

    // No conflict with unused key combo (Cmd+Ctrl+A is not used)
    conflict = keybindings_find_conflict(&config, KEY_A, MOD_SUPER | MOD_CTRL, ACTION_NONE);
    TEST_ASSERT_EQ(ACTION_NONE, conflict, "Should not detect conflict with unused key");

    // Exclude the action itself
    conflict = keybindings_find_conflict(&config, KEY_C, MOD_SUPER, ACTION_COPY);
    TEST_ASSERT_EQ(ACTION_NONE, conflict, "Should not conflict with itself");

    keybindings_free(&config);
}

// Test action name lookup
static void test_keybindings_action_name(void)
{
    printf("  Testing keybindings_action_name...\n");

    TEST_ASSERT_STR_EQ("copy", keybindings_action_name(ACTION_COPY), "Copy action name");
    TEST_ASSERT_STR_EQ("paste", keybindings_action_name(ACTION_PASTE), "Paste action name");
    TEST_ASSERT_STR_EQ("new_file", keybindings_action_name(ACTION_NEW_FILE), "New file action name");
    TEST_ASSERT_STR_EQ("command_palette", keybindings_action_name(ACTION_COMMAND_PALETTE), "Command palette action name");
    TEST_ASSERT_STR_EQ("none", keybindings_action_name(ACTION_NONE), "None action name");
}

// Test shortcut string generation
static void test_keybindings_shortcut_string(void)
{
    printf("  Testing keybindings_shortcut_string...\n");

    KeyBindingConfig config;
    keybindings_init(&config);

    // Get copy binding and check its string
    const KeyBinding *copy = keybindings_get(&config, ACTION_COPY);
    const char *shortcut = keybindings_shortcut_string(copy);
    TEST_ASSERT_STR_EQ("Cmd+C", shortcut, "Copy shortcut string");

    // Get cut binding
    const KeyBinding *cut = keybindings_get(&config, ACTION_CUT);
    shortcut = keybindings_shortcut_string(cut);
    TEST_ASSERT_STR_EQ("Cmd+X", shortcut, "Cut shortcut string");

    // Create binding with multiple modifiers
    keybindings_set(&config, ACTION_NEW_FILE, KEY_N, MOD_SUPER | MOD_SHIFT);
    const KeyBinding *new_file = keybindings_get(&config, ACTION_NEW_FILE);
    shortcut = keybindings_shortcut_string(new_file);
    TEST_ASSERT_STR_EQ("Cmd+Shift+N", shortcut, "New file shortcut with modifiers");

    // NULL binding
    shortcut = keybindings_shortcut_string(NULL);
    TEST_ASSERT_STR_EQ("", shortcut, "NULL binding returns empty string");

    keybindings_free(&config);
}

// Test shortcut parsing
static void test_keybindings_parse_shortcut(void)
{
    printf("  Testing keybindings_parse_shortcut...\n");

    int key, modifiers;

    // Simple key
    bool result = keybindings_parse_shortcut("A", &key, &modifiers);
    TEST_ASSERT(result, "Parse 'A' should succeed");
    TEST_ASSERT_EQ(KEY_A, key, "Key should be A");
    TEST_ASSERT_EQ(MOD_NONE, modifiers, "No modifiers");

    // Cmd+key
    result = keybindings_parse_shortcut("Cmd+C", &key, &modifiers);
    TEST_ASSERT(result, "Parse 'Cmd+C' should succeed");
    TEST_ASSERT_EQ(KEY_C, key, "Key should be C");
    TEST_ASSERT_EQ(MOD_SUPER, modifiers, "Should have Cmd modifier");

    // Multiple modifiers
    result = keybindings_parse_shortcut("Cmd+Shift+N", &key, &modifiers);
    TEST_ASSERT(result, "Parse 'Cmd+Shift+N' should succeed");
    TEST_ASSERT_EQ(KEY_N, key, "Key should be N");
    TEST_ASSERT_EQ(MOD_SUPER | MOD_SHIFT, modifiers, "Should have Cmd+Shift");

    // Ctrl+Alt+key
    result = keybindings_parse_shortcut("Ctrl+Alt+T", &key, &modifiers);
    TEST_ASSERT(result, "Parse 'Ctrl+Alt+T' should succeed");
    TEST_ASSERT_EQ(KEY_T, key, "Key should be T");
    TEST_ASSERT_EQ(MOD_CTRL | MOD_ALT, modifiers, "Should have Ctrl+Alt");

    // Named key
    result = keybindings_parse_shortcut("Cmd+Enter", &key, &modifiers);
    TEST_ASSERT(result, "Parse 'Cmd+Enter' should succeed");
    TEST_ASSERT_EQ(KEY_ENTER, key, "Key should be Enter");

    // Tab
    result = keybindings_parse_shortcut("Ctrl+Tab", &key, &modifiers);
    TEST_ASSERT(result, "Parse 'Ctrl+Tab' should succeed");
    TEST_ASSERT_EQ(KEY_TAB, key, "Key should be Tab");
    TEST_ASSERT_EQ(MOD_CTRL, modifiers, "Should have Ctrl");

    // Escape
    result = keybindings_parse_shortcut("Escape", &key, &modifiers);
    TEST_ASSERT(result, "Parse 'Escape' should succeed");
    TEST_ASSERT_EQ(KEY_ESCAPE, key, "Key should be Escape");

    // Alternative modifier names
    result = keybindings_parse_shortcut("Command+Option+K", &key, &modifiers);
    TEST_ASSERT(result, "Parse 'Command+Option+K' should succeed");
    TEST_ASSERT_EQ(KEY_K, key, "Key should be K");
    TEST_ASSERT_EQ(MOD_SUPER | MOD_ALT, modifiers, "Should have Cmd+Alt");

    // With spaces
    result = keybindings_parse_shortcut(" Cmd + P ", &key, &modifiers);
    TEST_ASSERT(result, "Parse ' Cmd + P ' should succeed");
    TEST_ASSERT_EQ(KEY_P, key, "Key should be P");

    // Invalid input
    result = keybindings_parse_shortcut(NULL, &key, &modifiers);
    TEST_ASSERT(!result, "Parse NULL should fail");

    result = keybindings_parse_shortcut("Cmd+", &key, &modifiers);
    TEST_ASSERT(!result, "Parse 'Cmd+' without key should fail");
}

// Test config file save and load
static void test_keybindings_save_load(void)
{
    printf("  Testing keybindings_save and keybindings_load...\n");

    const char *test_path = "/tmp/test_keybindings.conf";

    KeyBindingConfig config1;
    keybindings_init(&config1);

    // Modify some bindings
    keybindings_set(&config1, ACTION_COPY, KEY_D, MOD_SUPER | MOD_SHIFT);
    keybindings_set(&config1, ACTION_NEW_TAB, KEY_T, MOD_CTRL);

    // Save
    bool result = keybindings_save(&config1, test_path);
    TEST_ASSERT(result, "Save should succeed");

    // Load into new config
    KeyBindingConfig config2;
    keybindings_init(&config2);

    result = keybindings_load(&config2, test_path);
    TEST_ASSERT(result, "Load should succeed");

    // Verify loaded bindings
    const KeyBinding *copy = keybindings_get(&config2, ACTION_COPY);
    TEST_ASSERT(copy != NULL, "Copy binding should exist");
    TEST_ASSERT_EQ(KEY_D, copy->key, "Copy key should be D");
    TEST_ASSERT_EQ(MOD_SUPER | MOD_SHIFT, copy->modifiers, "Copy modifiers should match");

    const KeyBinding *new_tab = keybindings_get(&config2, ACTION_NEW_TAB);
    TEST_ASSERT(new_tab != NULL, "New tab binding should exist");
    TEST_ASSERT_EQ(KEY_T, new_tab->key, "New tab key should be T");
    TEST_ASSERT_EQ(MOD_CTRL, new_tab->modifiers, "New tab modifiers should match");

    // Clean up
    remove(test_path);
    keybindings_free(&config1);
    keybindings_free(&config2);
}

// Test modifier flags
static void test_keybindings_modifiers(void)
{
    printf("  Testing keybinding modifiers...\n");

    TEST_ASSERT_EQ(0, MOD_NONE, "MOD_NONE should be 0");
    TEST_ASSERT_EQ(1, MOD_CTRL, "MOD_CTRL should be 1");
    TEST_ASSERT_EQ(2, MOD_SHIFT, "MOD_SHIFT should be 2");
    TEST_ASSERT_EQ(4, MOD_ALT, "MOD_ALT should be 4");
    TEST_ASSERT_EQ(8, MOD_SUPER, "MOD_SUPER should be 8");

    // Test combinations
    int mods = MOD_CTRL | MOD_SHIFT;
    TEST_ASSERT(mods & MOD_CTRL, "Should have Ctrl");
    TEST_ASSERT(mods & MOD_SHIFT, "Should have Shift");
    TEST_ASSERT(!(mods & MOD_ALT), "Should not have Alt");
    TEST_ASSERT(!(mods & MOD_SUPER), "Should not have Super");
}

// Main test function
void test_keybindings(void)
{
    test_keybindings_init();
    test_keybindings_set_get();
    test_keybindings_remove();
    test_keybindings_reset_defaults();
    test_keybindings_conflict();
    test_keybindings_action_name();
    test_keybindings_shortcut_string();
    test_keybindings_parse_shortcut();
    test_keybindings_save_load();
    test_keybindings_modifiers();
}

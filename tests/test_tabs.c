#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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

// Tab structures (copy of ui/tabs.h definitions for testing)
#define MAX_TABS 32
#define PATH_MAX_LEN 4096
#define NAME_MAX_LEN 256

typedef struct Tab {
    char path[PATH_MAX_LEN];
    char title[NAME_MAX_LEN];
    int selected_index;
    int scroll_offset;
    bool active;
} Tab;

typedef struct TabState {
    Tab tabs[MAX_TABS];
    int count;
    int current;
    int hovered;
    bool dragging;
    int drag_index;
} TabState;

// Tab functions implementation for testing
static void tabs_init(TabState *state)
{
    memset(state, 0, sizeof(TabState));
    state->count = 0;
    state->current = -1;
    state->hovered = -1;
    state->dragging = false;
    state->drag_index = -1;
}

static const char* get_title_from_path(const char *path)
{
    if (!path || path[0] == '\0') return "New Tab";

    const char *last_slash = strrchr(path, '/');
    if (last_slash && last_slash[1] != '\0') {
        return last_slash + 1;
    }

    if (strcmp(path, "/") == 0) {
        return "/";
    }

    return path;
}

static int tabs_new(TabState *state, const char *path)
{
    if (state->count >= MAX_TABS) {
        return -1;
    }

    int index = state->count;
    Tab *tab = &state->tabs[index];

    strncpy(tab->path, path ? path : "", PATH_MAX_LEN - 1);
    tab->path[PATH_MAX_LEN - 1] = '\0';

    const char *title = get_title_from_path(path);
    strncpy(tab->title, title, NAME_MAX_LEN - 1);
    tab->title[NAME_MAX_LEN - 1] = '\0';

    tab->selected_index = 0;
    tab->scroll_offset = 0;
    tab->active = true;

    state->count++;
    state->current = index;

    return index;
}

static void tabs_close(TabState *state, int index)
{
    if (index < 0 || index >= state->count) {
        return;
    }

    // Don't close the last tab
    if (state->count <= 1) {
        return;
    }

    // Shift remaining tabs
    for (int i = index; i < state->count - 1; i++) {
        state->tabs[i] = state->tabs[i + 1];
    }

    state->count--;

    // Adjust current tab index
    if (state->current >= state->count) {
        state->current = state->count - 1;
    } else if (state->current > index) {
        state->current--;
    }
}

static void tabs_switch(TabState *state, int index)
{
    if (index >= 0 && index < state->count) {
        state->current = index;
    }
}

static Tab* tabs_get_current(TabState *state)
{
    if (state->current >= 0 && state->current < state->count) {
        return &state->tabs[state->current];
    }
    return NULL;
}

void test_tabs(void)
{
    // Test tabs_init
    {
        TabState state;
        tabs_init(&state);

        TEST_ASSERT_EQ(0, state.count, "Initial count should be 0");
        TEST_ASSERT_EQ(-1, state.current, "Initial current should be -1");
        TEST_ASSERT_EQ(-1, state.hovered, "Initial hovered should be -1");
        TEST_ASSERT(state.dragging == false, "Initial dragging should be false");
    }

    // Test tabs_new
    {
        TabState state;
        tabs_init(&state);

        int idx = tabs_new(&state, "/home/user");
        TEST_ASSERT_EQ(0, idx, "First tab index should be 0");
        TEST_ASSERT_EQ(1, state.count, "Count should be 1");
        TEST_ASSERT_EQ(0, state.current, "Current should be 0");
        TEST_ASSERT_STR_EQ("/home/user", state.tabs[0].path, "Path should match");
        TEST_ASSERT_STR_EQ("user", state.tabs[0].title, "Title should be last path component");
    }

    // Test multiple tabs
    {
        TabState state;
        tabs_init(&state);

        tabs_new(&state, "/home/user");
        tabs_new(&state, "/tmp");
        tabs_new(&state, "/var/log");

        TEST_ASSERT_EQ(3, state.count, "Should have 3 tabs");
        TEST_ASSERT_EQ(2, state.current, "Current should be last opened");
        TEST_ASSERT_STR_EQ("user", state.tabs[0].title, "First tab title");
        TEST_ASSERT_STR_EQ("tmp", state.tabs[1].title, "Second tab title");
        TEST_ASSERT_STR_EQ("log", state.tabs[2].title, "Third tab title");
    }

    // Test tabs_switch
    {
        TabState state;
        tabs_init(&state);

        tabs_new(&state, "/one");
        tabs_new(&state, "/two");
        tabs_new(&state, "/three");

        tabs_switch(&state, 0);
        TEST_ASSERT_EQ(0, state.current, "Should switch to tab 0");

        tabs_switch(&state, 1);
        TEST_ASSERT_EQ(1, state.current, "Should switch to tab 1");

        // Invalid switch should be ignored
        tabs_switch(&state, 99);
        TEST_ASSERT_EQ(1, state.current, "Invalid switch should be ignored");

        tabs_switch(&state, -1);
        TEST_ASSERT_EQ(1, state.current, "Negative switch should be ignored");
    }

    // Test tabs_close
    {
        TabState state;
        tabs_init(&state);

        tabs_new(&state, "/one");
        tabs_new(&state, "/two");
        tabs_new(&state, "/three");

        tabs_close(&state, 1);
        TEST_ASSERT_EQ(2, state.count, "Should have 2 tabs after close");
        TEST_ASSERT_STR_EQ("one", state.tabs[0].title, "First tab should remain");
        TEST_ASSERT_STR_EQ("three", state.tabs[1].title, "Third tab should shift");
    }

    // Test closing current tab adjusts index
    {
        TabState state;
        tabs_init(&state);

        tabs_new(&state, "/one");
        tabs_new(&state, "/two");
        tabs_new(&state, "/three");

        tabs_switch(&state, 2);
        tabs_close(&state, 2);

        TEST_ASSERT_EQ(1, state.current, "Current should adjust when closing last tab");
    }

    // Test cannot close last tab
    {
        TabState state;
        tabs_init(&state);

        tabs_new(&state, "/only");
        tabs_close(&state, 0);

        TEST_ASSERT_EQ(1, state.count, "Should not close last tab");
        TEST_ASSERT_STR_EQ("only", state.tabs[0].title, "Last tab should remain");
    }

    // Test get_current
    {
        TabState state;
        tabs_init(&state);

        Tab *current = tabs_get_current(&state);
        TEST_ASSERT(current == NULL, "No current tab when empty");

        tabs_new(&state, "/test");
        current = tabs_get_current(&state);
        TEST_ASSERT(current != NULL, "Should have current tab");
        TEST_ASSERT_STR_EQ("/test", current->path, "Current tab path should match");
    }

    // Test root path title
    {
        TabState state;
        tabs_init(&state);

        tabs_new(&state, "/");
        TEST_ASSERT_STR_EQ("/", state.tabs[0].title, "Root path should have / as title");
    }

    // Test max tabs limit
    {
        TabState state;
        tabs_init(&state);

        for (int i = 0; i < MAX_TABS; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/tab%d", i);
            tabs_new(&state, path);
        }

        TEST_ASSERT_EQ(MAX_TABS, state.count, "Should have MAX_TABS tabs");

        int result = tabs_new(&state, "/overflow");
        TEST_ASSERT_EQ(-1, result, "Should not add beyond MAX_TABS");
        TEST_ASSERT_EQ(MAX_TABS, state.count, "Count should stay at MAX_TABS");
    }

    // Test tab state preservation
    {
        TabState state;
        tabs_init(&state);

        tabs_new(&state, "/test");
        state.tabs[0].selected_index = 42;
        state.tabs[0].scroll_offset = 10;

        tabs_new(&state, "/other");
        tabs_switch(&state, 0);

        Tab *current = tabs_get_current(&state);
        TEST_ASSERT_EQ(42, current->selected_index, "Selected index should be preserved");
        TEST_ASSERT_EQ(10, current->scroll_offset, "Scroll offset should be preserved");
    }
}

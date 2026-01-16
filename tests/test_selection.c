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

// Selection state (copy of app.h definition)
#define MAX_SELECTION 4096

typedef struct SelectionState {
    int *indices;
    int count;
    int capacity;
    int anchor_index;
} SelectionState;

// Implement selection functions locally for testing
static void selection_init(SelectionState *sel)
{
    sel->indices = NULL;
    sel->count = 0;
    sel->capacity = 0;
    sel->anchor_index = -1;
}

static void selection_free(SelectionState *sel)
{
    if (sel->indices) {
        free(sel->indices);
        sel->indices = NULL;
    }
    sel->count = 0;
    sel->capacity = 0;
    sel->anchor_index = -1;
}

static void selection_clear(SelectionState *sel)
{
    sel->count = 0;
    sel->anchor_index = -1;
}

static bool selection_ensure_capacity(SelectionState *sel, int needed)
{
    if (needed <= sel->capacity) {
        return true;
    }

    int new_capacity = sel->capacity == 0 ? 16 : sel->capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    if (new_capacity > MAX_SELECTION) {
        new_capacity = MAX_SELECTION;
    }

    int *new_indices = realloc(sel->indices, new_capacity * sizeof(int));
    if (!new_indices) {
        return false;
    }

    sel->indices = new_indices;
    sel->capacity = new_capacity;
    return true;
}

static bool selection_contains(SelectionState *sel, int index)
{
    for (int i = 0; i < sel->count; i++) {
        if (sel->indices[i] == index) {
            return true;
        }
    }
    return false;
}

static void selection_add(SelectionState *sel, int index)
{
    if (selection_contains(sel, index)) {
        return;
    }

    if (!selection_ensure_capacity(sel, sel->count + 1)) {
        return;
    }

    sel->indices[sel->count] = index;
    sel->count++;
}

static void selection_remove(SelectionState *sel, int index)
{
    for (int i = 0; i < sel->count; i++) {
        if (sel->indices[i] == index) {
            for (int j = i; j < sel->count - 1; j++) {
                sel->indices[j] = sel->indices[j + 1];
            }
            sel->count--;
            return;
        }
    }
}

static void selection_toggle(SelectionState *sel, int index)
{
    if (selection_contains(sel, index)) {
        selection_remove(sel, index);
    } else {
        selection_add(sel, index);
    }
}

static void selection_range(SelectionState *sel, int from, int to)
{
    selection_clear(sel);

    int start = from < to ? from : to;
    int end = from < to ? to : from;

    for (int i = start; i <= end; i++) {
        selection_add(sel, i);
    }
}

void test_selection(void)
{
    // Test selection_init
    {
        SelectionState sel;
        selection_init(&sel);

        TEST_ASSERT(sel.indices == NULL, "Initial indices should be NULL");
        TEST_ASSERT_EQ(0, sel.count, "Initial count should be 0");
        TEST_ASSERT_EQ(0, sel.capacity, "Initial capacity should be 0");
        TEST_ASSERT_EQ(-1, sel.anchor_index, "Initial anchor should be -1");

        selection_free(&sel);
    }

    // Test selection_add
    {
        SelectionState sel;
        selection_init(&sel);

        selection_add(&sel, 5);
        TEST_ASSERT_EQ(1, sel.count, "Count should be 1 after add");
        TEST_ASSERT(selection_contains(&sel, 5), "Should contain index 5");

        selection_add(&sel, 10);
        TEST_ASSERT_EQ(2, sel.count, "Count should be 2 after second add");
        TEST_ASSERT(selection_contains(&sel, 10), "Should contain index 10");

        // Adding duplicate should not increase count
        selection_add(&sel, 5);
        TEST_ASSERT_EQ(2, sel.count, "Count should still be 2 (no duplicates)");

        selection_free(&sel);
    }

    // Test selection_remove
    {
        SelectionState sel;
        selection_init(&sel);

        selection_add(&sel, 1);
        selection_add(&sel, 2);
        selection_add(&sel, 3);

        selection_remove(&sel, 2);
        TEST_ASSERT_EQ(2, sel.count, "Count should be 2 after remove");
        TEST_ASSERT(!selection_contains(&sel, 2), "Should not contain removed index");
        TEST_ASSERT(selection_contains(&sel, 1), "Should still contain 1");
        TEST_ASSERT(selection_contains(&sel, 3), "Should still contain 3");

        // Removing non-existent should be safe
        selection_remove(&sel, 99);
        TEST_ASSERT_EQ(2, sel.count, "Count should still be 2");

        selection_free(&sel);
    }

    // Test selection_toggle
    {
        SelectionState sel;
        selection_init(&sel);

        selection_toggle(&sel, 5);
        TEST_ASSERT(selection_contains(&sel, 5), "Should contain 5 after toggle on");

        selection_toggle(&sel, 5);
        TEST_ASSERT(!selection_contains(&sel, 5), "Should not contain 5 after toggle off");
        TEST_ASSERT_EQ(0, sel.count, "Count should be 0 after toggle off");

        selection_free(&sel);
    }

    // Test selection_clear
    {
        SelectionState sel;
        selection_init(&sel);

        selection_add(&sel, 1);
        selection_add(&sel, 2);
        selection_add(&sel, 3);

        selection_clear(&sel);
        TEST_ASSERT_EQ(0, sel.count, "Count should be 0 after clear");
        TEST_ASSERT_EQ(-1, sel.anchor_index, "Anchor should be -1 after clear");

        selection_free(&sel);
    }

    // Test selection_range
    {
        SelectionState sel;
        selection_init(&sel);

        selection_range(&sel, 3, 7);
        TEST_ASSERT_EQ(5, sel.count, "Range 3-7 should have 5 items");
        TEST_ASSERT(selection_contains(&sel, 3), "Should contain 3");
        TEST_ASSERT(selection_contains(&sel, 5), "Should contain 5");
        TEST_ASSERT(selection_contains(&sel, 7), "Should contain 7");
        TEST_ASSERT(!selection_contains(&sel, 2), "Should not contain 2");
        TEST_ASSERT(!selection_contains(&sel, 8), "Should not contain 8");

        selection_free(&sel);
    }

    // Test selection_range (reverse)
    {
        SelectionState sel;
        selection_init(&sel);

        selection_range(&sel, 7, 3);
        TEST_ASSERT_EQ(5, sel.count, "Range 7-3 should also have 5 items");
        TEST_ASSERT(selection_contains(&sel, 3), "Should contain 3");
        TEST_ASSERT(selection_contains(&sel, 7), "Should contain 7");

        selection_free(&sel);
    }

    // Test capacity growth
    {
        SelectionState sel;
        selection_init(&sel);

        for (int i = 0; i < 100; i++) {
            selection_add(&sel, i);
        }

        TEST_ASSERT_EQ(100, sel.count, "Should have 100 items");
        TEST_ASSERT(sel.capacity >= 100, "Capacity should be at least 100");

        bool all_present = true;
        for (int i = 0; i < 100; i++) {
            if (!selection_contains(&sel, i)) {
                all_present = false;
                break;
            }
        }
        TEST_ASSERT(all_present, "Should contain all 100 indices");

        selection_free(&sel);
    }
}

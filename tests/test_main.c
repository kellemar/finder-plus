#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple test framework
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("  PASS: %s\n", message); \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s (line %d)\n", message, __LINE__); \
    } \
} while(0)

#define TEST_ASSERT_EQ(expected, actual, message) do { \
    tests_run++; \
    if ((expected) == (actual)) { \
        tests_passed++; \
        printf("  PASS: %s\n", message); \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s - expected %d, got %d (line %d)\n", message, (int)(expected), (int)(actual), __LINE__); \
    } \
} while(0)

#define TEST_ASSERT_STR_EQ(expected, actual, message) do { \
    tests_run++; \
    if (strcmp((expected), (actual)) == 0) { \
        tests_passed++; \
        printf("  PASS: %s\n", message); \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s - expected '%s', got '%s' (line %d)\n", message, (expected), (actual), __LINE__); \
    } \
} while(0)

// External test functions
extern void test_filesystem(void);
extern void test_selection(void);
extern void test_operations(void);
extern void test_search(void);
extern void test_config(void);
extern void test_tabs(void);
extern void test_tool_registry(void);
extern void test_http_client(void);
extern void test_claude_client(void);
extern void test_tool_executor(void);
extern void test_ai(void);
extern void test_phase6(void);
extern void test_git(void);
extern void test_operation_queue(void);
extern void test_keybindings(void);
extern void test_dialog(void);
extern void test_context_menu(void);
extern void test_dual_pane(void);
extern void test_network(void);
extern void test_perf(void);
extern void test_phase2(void);
extern void test_video(void);
extern void test_preview(void);
extern void test_gemini_client(void);
extern void test_font(void);
extern void test_progress_indicator(void);
extern void test_file_view_modal(void);

int main(void)
{
    printf("\n=== Finder Plus Test Suite ===\n\n");

    printf("[Filesystem Tests]\n");
    test_filesystem();

    printf("\n[Selection Tests]\n");
    test_selection();

    printf("\n[Operations Tests]\n");
    test_operations();

    printf("\n[Search Tests]\n");
    test_search();

    printf("\n[Config Tests]\n");
    test_config();

    printf("\n[Tabs Tests]\n");
    test_tabs();

    printf("\n[Tool Registry Tests]\n");
    test_tool_registry();

    printf("\n[HTTP Client Tests]\n");
    test_http_client();

    printf("\n[Claude Client Tests]\n");
    test_claude_client();

    test_gemini_client();

    printf("\n[Tool Executor Tests]\n");
    test_tool_executor();

    printf("\n[AI Tests]\n");
    test_ai();

    printf("\n[Phase 6 Tests]\n");
    test_phase6();

    printf("\n[Git Tests]\n");
    test_git();

    printf("\n[Operation Queue Tests]\n");
    test_operation_queue();

    printf("\n[Keybindings Tests]\n");
    test_keybindings();

    printf("\n[Dialog Tests]\n");
    test_dialog();

    printf("\n[Context Menu Tests]\n");
    test_context_menu();

    printf("\n[Dual Pane Tests]\n");
    test_dual_pane();

    printf("\n[Network Tests]\n");
    test_network();

    printf("\n[Performance Tests]\n");
    test_perf();

    printf("\n[Phase 2 Tests]\n");
    test_phase2();

    printf("\n[Video Tests]\n");
    test_video();

    printf("\n[Preview Tests]\n");
    test_preview();

    printf("\n[Font Tests]\n");
    test_font();

    printf("\n[Progress Indicator Tests]\n");
    test_progress_indicator();

    printf("\n[File View Modal Tests]\n");
    test_file_view_modal();

    printf("\n=== Results ===\n");
    printf("Passed: %d/%d\n", tests_passed, tests_run);
    printf("Failed: %d\n", tests_failed);

    if (tests_failed > 0) {
        printf("\nSome tests FAILED!\n");
        return 1;
    }

    printf("\nAll tests PASSED!\n");
    return 0;
}

// Export test macros for other test files
int get_tests_run(void) { return tests_run; }
int get_tests_passed(void) { return tests_passed; }
int get_tests_failed(void) { return tests_failed; }
void inc_tests_run(void) { tests_run++; }
void inc_tests_passed(void) { tests_passed++; }
void inc_tests_failed(void) { tests_failed++; }

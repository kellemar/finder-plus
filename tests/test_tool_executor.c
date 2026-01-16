#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tools/tool_executor.h"
#include "tools/tool_registry.h"
#include "cJSON/cJSON.h"

// Test macros from test_main.c
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

static char test_dir[256];

static void setup_test_environment(void)
{
    snprintf(test_dir, sizeof(test_dir), "/tmp/finder_plus_executor_test_%d", getpid());
    mkdir(test_dir, 0755);

    // Create some test files
    char path[512];
    snprintf(path, sizeof(path), "%s/file1.txt", test_dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "test content 1");
        fclose(f);
    }

    snprintf(path, sizeof(path), "%s/file2.txt", test_dir);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "test content 2");
        fclose(f);
    }

    snprintf(path, sizeof(path), "%s/subdir", test_dir);
    mkdir(path, 0755);
}

static void cleanup_test_environment(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_dir);
    system(cmd);
}

static void test_executor_create_destroy(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);

    ToolExecutor *executor = tool_executor_create(registry);
    TEST_ASSERT(executor != NULL, "Create executor");
    TEST_ASSERT(executor->registry == registry, "Executor has registry");

    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
    printf("  PASS: Destroy executor (no crash)\n");
    inc_tests_run();
    inc_tests_passed();
}

static void test_executor_set_cwd(void)
{
    ToolRegistry *registry = tool_registry_create();
    ToolExecutor *executor = tool_executor_create(registry);

    tool_executor_set_cwd(executor, "/Users/test");
    TEST_ASSERT(strcmp(executor->current_dir, "/Users/test") == 0, "Set CWD");

    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
}

static void test_execute_file_list(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);
    ToolExecutor *executor = tool_executor_create(registry);

    char input[256];
    snprintf(input, sizeof(input), "{\"path\": \"%s\"}", test_dir);

    ToolResult result = tool_executor_execute(executor, "file_list", input);

    TEST_ASSERT(result.success == true, "file_list succeeds");
    TEST_ASSERT(result.output != NULL, "file_list has output");

    if (result.output) {
        TEST_ASSERT(strstr(result.output, "file1.txt") != NULL, "Lists file1.txt");
        TEST_ASSERT(strstr(result.output, "file2.txt") != NULL, "Lists file2.txt");
        TEST_ASSERT(strstr(result.output, "subdir") != NULL, "Lists subdir");
    }

    tool_result_cleanup(&result);
    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
}

static void test_execute_file_metadata(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);
    ToolExecutor *executor = tool_executor_create(registry);

    char input[256];
    snprintf(input, sizeof(input), "{\"path\": \"%s/file1.txt\"}", test_dir);

    ToolResult result = tool_executor_execute(executor, "file_metadata", input);

    TEST_ASSERT(result.success == true, "file_metadata succeeds");
    TEST_ASSERT(result.output != NULL, "file_metadata has output");

    if (result.output) {
        TEST_ASSERT(strstr(result.output, "file1.txt") != NULL, "Metadata contains filename");
        // Check for is_file being true (cJSON uses various formats)
        TEST_ASSERT(strstr(result.output, "\"is_file\"") != NULL &&
                    strstr(result.output, "true") != NULL, "Has is_file field");
    }

    tool_result_cleanup(&result);
    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
}

static void test_execute_file_create(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);
    ToolExecutor *executor = tool_executor_create(registry);

    char input[256];
    snprintf(input, sizeof(input), "{\"path\": \"%s/newfile.txt\"}", test_dir);

    ToolResult result = tool_executor_execute(executor, "file_create", input);

    TEST_ASSERT(result.success == true, "file_create succeeds");

    // Verify file exists
    char path[512];
    snprintf(path, sizeof(path), "%s/newfile.txt", test_dir);
    struct stat st;
    TEST_ASSERT(stat(path, &st) == 0, "New file exists");

    tool_result_cleanup(&result);
    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
}

static void test_execute_file_create_directory(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);
    ToolExecutor *executor = tool_executor_create(registry);

    char input[256];
    snprintf(input, sizeof(input), "{\"path\": \"%s/newdir\", \"is_directory\": true}", test_dir);

    ToolResult result = tool_executor_execute(executor, "file_create", input);

    TEST_ASSERT(result.success == true, "file_create directory succeeds");

    // Verify directory exists
    char path[512];
    snprintf(path, sizeof(path), "%s/newdir", test_dir);
    struct stat st;
    TEST_ASSERT(stat(path, &st) == 0, "New directory exists");
    TEST_ASSERT(S_ISDIR(st.st_mode), "Is a directory");

    tool_result_cleanup(&result);
    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
}

static void test_execute_file_create_with_content(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);
    ToolExecutor *executor = tool_executor_create(registry);

    const char *content = "This is AI-generated content for testing.\\nLine 2 of the file.";
    char input[1024];
    snprintf(input, sizeof(input),
             "{\"path\": \"%s/ai_generated.txt\", \"content\": \"%s\"}",
             test_dir, content);

    ToolResult result = tool_executor_execute(executor, "file_create", input);

    TEST_ASSERT(result.success == true, "file_create with content succeeds");
    TEST_ASSERT(strstr(result.output, "with content") != NULL, "Output mentions content");

    // Verify file exists and has correct content
    char path[512];
    snprintf(path, sizeof(path), "%s/ai_generated.txt", test_dir);
    struct stat st;
    TEST_ASSERT(stat(path, &st) == 0, "File with content exists");
    TEST_ASSERT(S_ISREG(st.st_mode), "Is a regular file");

    // Read and verify content
    FILE *f = fopen(path, "r");
    TEST_ASSERT(f != NULL, "Can open file for reading");
    if (f) {
        char buffer[1024];
        size_t bytes = fread(buffer, 1, sizeof(buffer) - 1, f);
        buffer[bytes] = '\0';
        fclose(f);

        // Note: JSON escapes \n as \\n so the actual file has literal \n chars
        TEST_ASSERT(strstr(buffer, "AI-generated") != NULL, "Content contains expected text");
    }

    tool_result_cleanup(&result);
    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
}

static void test_execute_file_search(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);
    ToolExecutor *executor = tool_executor_create(registry);

    char input[256];
    snprintf(input, sizeof(input), "{\"path\": \"%s\", \"pattern\": \"*.txt\"}", test_dir);

    ToolResult result = tool_executor_execute(executor, "file_search", input);

    TEST_ASSERT(result.success == true, "file_search succeeds");
    TEST_ASSERT(result.output != NULL, "file_search has output");

    if (result.output) {
        TEST_ASSERT(strstr(result.output, "file1.txt") != NULL, "Found file1.txt");
        TEST_ASSERT(strstr(result.output, "file2.txt") != NULL, "Found file2.txt");
    }

    tool_result_cleanup(&result);
    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
}

static void test_execute_unknown_tool(void)
{
    ToolRegistry *registry = tool_registry_create();
    ToolExecutor *executor = tool_executor_create(registry);

    ToolResult result = tool_executor_execute(executor, "nonexistent_tool", "{}");

    TEST_ASSERT(result.success == false, "Unknown tool fails");
    TEST_ASSERT(result.error != NULL, "Has error message");
    TEST_ASSERT(strstr(result.error, "Unknown tool") != NULL, "Error mentions unknown tool");

    tool_result_cleanup(&result);
    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
}

static void test_execute_invalid_json(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);
    ToolExecutor *executor = tool_executor_create(registry);

    ToolResult result = tool_executor_execute(executor, "file_list", "not valid json{");

    TEST_ASSERT(result.success == false, "Invalid JSON fails");
    TEST_ASSERT(result.error != NULL, "Has error message");

    tool_result_cleanup(&result);
    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
}

static void test_prepare_pending_operation(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);
    ToolExecutor *executor = tool_executor_create(registry);

    PendingOperation pending;
    char input[256];
    snprintf(input, sizeof(input), "{\"paths\": [\"%s/file1.txt\"]}", test_dir);

    bool prepared = tool_executor_prepare(executor, "file_delete", "tool_123", input, &pending);

    TEST_ASSERT(prepared == true, "Prepare succeeds");
    TEST_ASSERT(strcmp(pending.tool_id, "tool_123") == 0, "Tool ID set");
    TEST_ASSERT(strcmp(pending.tool_name, "file_delete") == 0, "Tool name set");
    TEST_ASSERT(pending.requires_confirmation == true, "Requires confirmation");
    TEST_ASSERT(strlen(pending.description) > 0, "Has description");

    tool_executor_cancel(&pending);
    tool_executor_destroy(executor);
    tool_registry_destroy(registry);
}

static void test_describe_operation(void)
{
    char buffer[256];

    tool_executor_describe_operation("file_list", "{\"path\": \"/Users\"}", buffer, sizeof(buffer));
    TEST_ASSERT(strstr(buffer, "/Users") != NULL, "Description contains path");

    tool_executor_describe_operation("file_delete", "{\"paths\": [\"/tmp/a\", \"/tmp/b\"]}", buffer, sizeof(buffer));
    TEST_ASSERT(strstr(buffer, "Trash") != NULL || strstr(buffer, "delete") != NULL,
                "Description mentions trash/delete");

    tool_executor_describe_operation("file_rename", "{\"new_name\": \"newname.txt\"}", buffer, sizeof(buffer));
    TEST_ASSERT(strstr(buffer, "newname.txt") != NULL, "Description contains new name");
}

void test_tool_executor(void)
{
    setup_test_environment();

    test_executor_create_destroy();
    test_executor_set_cwd();
    test_execute_file_list();
    test_execute_file_metadata();
    test_execute_file_create();
    test_execute_file_create_directory();
    test_execute_file_create_with_content();
    test_execute_file_search();
    test_execute_unknown_tool();
    test_execute_invalid_json();
    test_prepare_pending_operation();
    test_describe_operation();

    cleanup_test_environment();
}

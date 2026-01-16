#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void test_registry_create_destroy(void)
{
    ToolRegistry *registry = tool_registry_create();
    TEST_ASSERT(registry != NULL, "Create tool registry");
    TEST_ASSERT(tool_registry_count(registry) == 0, "New registry has 0 tools");

    tool_registry_destroy(registry);
    printf("  PASS: Destroy tool registry (no crash)\n");
    inc_tests_run();
    inc_tests_passed();
}

static void test_registry_add_tool(void)
{
    ToolRegistry *registry = tool_registry_create();

    ToolDefinition tool = {0};
    strncpy(tool.name, "test_tool", TOOL_MAX_NAME_LEN - 1);
    strncpy(tool.description, "A test tool", TOOL_MAX_DESC_LEN - 1);
    tool.requires_confirmation = true;

    tool_registry_add(registry, &tool);
    TEST_ASSERT(tool_registry_count(registry) == 1, "Registry has 1 tool after add");

    const ToolDefinition *found = tool_registry_find(registry, "test_tool");
    TEST_ASSERT(found != NULL, "Find added tool by name");
    TEST_ASSERT(strcmp(found->name, "test_tool") == 0, "Found tool has correct name");
    TEST_ASSERT(found->requires_confirmation == true, "Found tool has correct confirmation flag");

    const ToolDefinition *not_found = tool_registry_find(registry, "nonexistent");
    TEST_ASSERT(not_found == NULL, "Find returns NULL for nonexistent tool");

    tool_registry_destroy(registry);
}

static void test_registry_file_tools(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);

    int count = tool_registry_count(registry);
    TEST_ASSERT(count >= 10, "File tools registered (expected >= 10 tools)");

    // Check specific tools exist
    TEST_ASSERT(tool_registry_find(registry, "file_list") != NULL, "file_list tool exists");
    TEST_ASSERT(tool_registry_find(registry, "file_move") != NULL, "file_move tool exists");
    TEST_ASSERT(tool_registry_find(registry, "file_copy") != NULL, "file_copy tool exists");
    TEST_ASSERT(tool_registry_find(registry, "file_delete") != NULL, "file_delete tool exists");
    TEST_ASSERT(tool_registry_find(registry, "file_create") != NULL, "file_create tool exists");
    TEST_ASSERT(tool_registry_find(registry, "file_rename") != NULL, "file_rename tool exists");
    TEST_ASSERT(tool_registry_find(registry, "file_search") != NULL, "file_search tool exists");
    TEST_ASSERT(tool_registry_find(registry, "batch_rename") != NULL, "batch_rename tool exists");

    // Check confirmation flags
    const ToolDefinition *file_list = tool_registry_find(registry, "file_list");
    TEST_ASSERT(file_list->requires_confirmation == false, "file_list does not require confirmation");

    const ToolDefinition *file_delete = tool_registry_find(registry, "file_delete");
    TEST_ASSERT(file_delete->requires_confirmation == true, "file_delete requires confirmation");

    tool_registry_destroy(registry);
}

static void test_registry_to_json(void)
{
    ToolRegistry *registry = tool_registry_create();
    tool_registry_register_file_tools(registry);

    cJSON *json = tool_registry_to_json(registry);
    TEST_ASSERT(json != NULL, "Convert registry to JSON");
    TEST_ASSERT(cJSON_IsArray(json), "JSON is an array");

    int json_count = cJSON_GetArraySize(json);
    TEST_ASSERT(json_count == tool_registry_count(registry), "JSON array size matches tool count");

    // Check first tool structure
    cJSON *first_tool = cJSON_GetArrayItem(json, 0);
    TEST_ASSERT(first_tool != NULL, "First tool in JSON exists");
    TEST_ASSERT(cJSON_GetObjectItem(first_tool, "name") != NULL, "Tool has name field");
    TEST_ASSERT(cJSON_GetObjectItem(first_tool, "description") != NULL, "Tool has description field");
    TEST_ASSERT(cJSON_GetObjectItem(first_tool, "input_schema") != NULL, "Tool has input_schema field");

    cJSON_Delete(json);
    tool_registry_destroy(registry);
}

static void test_tool_result(void)
{
    ToolResult result;
    tool_result_init(&result);

    TEST_ASSERT(result.success == false, "Initial result not successful");
    TEST_ASSERT(result.output == NULL, "Initial result has no output");
    TEST_ASSERT(result.error == NULL, "Initial result has no error");

    tool_result_set_success(&result, "Operation completed", 5);
    TEST_ASSERT(result.success == true, "Result marked as successful");
    TEST_ASSERT(result.affected_count == 5, "Affected count is 5");
    TEST_ASSERT(strcmp(result.output, "Operation completed") == 0, "Output message correct");

    tool_result_cleanup(&result);
    TEST_ASSERT(result.output == NULL, "Output cleaned up");

    tool_result_init(&result);
    tool_result_set_error(&result, "Something failed");
    TEST_ASSERT(result.success == false, "Error result not successful");
    TEST_ASSERT(strcmp(result.error, "Something failed") == 0, "Error message correct");

    char *json = tool_result_to_json(&result);
    TEST_ASSERT(json != NULL, "Result converts to JSON");
    TEST_ASSERT(strstr(json, "\"success\":false") != NULL, "JSON contains success:false");
    TEST_ASSERT(strstr(json, "Something failed") != NULL, "JSON contains error message");
    free(json);

    tool_result_cleanup(&result);
}

static void test_param_type_string(void)
{
    TEST_ASSERT(strcmp(tool_param_type_to_string(TOOL_PARAM_STRING), "string") == 0, "STRING type string");
    TEST_ASSERT(strcmp(tool_param_type_to_string(TOOL_PARAM_INTEGER), "integer") == 0, "INTEGER type string");
    TEST_ASSERT(strcmp(tool_param_type_to_string(TOOL_PARAM_BOOLEAN), "boolean") == 0, "BOOLEAN type string");
    TEST_ASSERT(strcmp(tool_param_type_to_string(TOOL_PARAM_ARRAY), "array") == 0, "ARRAY type string");
    TEST_ASSERT(strcmp(tool_param_type_to_string(TOOL_PARAM_OBJECT), "object") == 0, "OBJECT type string");
}

void test_tool_registry(void)
{
    test_registry_create_destroy();
    test_registry_add_tool();
    test_registry_file_tools();
    test_registry_to_json();
    test_tool_result();
    test_param_type_string();
}

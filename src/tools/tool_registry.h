#ifndef TOOL_REGISTRY_H
#define TOOL_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>

#define TOOL_MAX_NAME_LEN 64
#define TOOL_MAX_DESC_LEN 512
#define MAX_TOOLS 16
#define MAX_TOOL_PARAMS 16

// Tool parameter types
typedef enum ToolParamType {
    TOOL_PARAM_STRING = 0,
    TOOL_PARAM_INTEGER,
    TOOL_PARAM_BOOLEAN,
    TOOL_PARAM_ARRAY,
    TOOL_PARAM_OBJECT
} ToolParamType;

// Tool parameter definition
typedef struct ToolParameter {
    char name[64];
    char description[256];
    ToolParamType type;
    bool required;
} ToolParameter;

// Tool definition
typedef struct ToolDefinition {
    char name[TOOL_MAX_NAME_LEN];
    char description[TOOL_MAX_DESC_LEN];
    ToolParameter params[MAX_TOOL_PARAMS];
    int param_count;
    bool requires_confirmation;
} ToolDefinition;

// Tool registry
typedef struct ToolRegistry {
    ToolDefinition tools[MAX_TOOLS];
    int tool_count;
} ToolRegistry;

// Tool result from execution
typedef struct ToolResult {
    bool success;
    char *output;
    char *error;
    int exit_code;
    int affected_count;
} ToolResult;

// Registry functions
ToolRegistry *tool_registry_create(void);
void tool_registry_destroy(ToolRegistry *registry);
void tool_registry_add(ToolRegistry *registry, const ToolDefinition *tool);
void tool_registry_register_file_tools(ToolRegistry *registry);
const ToolDefinition *tool_registry_find(const ToolRegistry *registry, const char *name);
int tool_registry_count(const ToolRegistry *registry);

// Tool result functions
void tool_result_init(ToolResult *result);
void tool_result_cleanup(ToolResult *result);
void tool_result_set_success(ToolResult *result, const char *output, int affected);
void tool_result_set_error(ToolResult *result, const char *error);

// JSON generation (forward declare cJSON)
struct cJSON;
struct cJSON *tool_definition_to_json(const ToolDefinition *tool);
struct cJSON *tool_registry_to_json(const ToolRegistry *registry);
char *tool_result_to_json(const ToolResult *result);

// Utility
const char *tool_param_type_to_string(ToolParamType type);
bool tool_requires_confirmation(const ToolDefinition *tool);

#endif // TOOL_REGISTRY_H

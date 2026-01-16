#ifndef TOOL_EXECUTOR_H
#define TOOL_EXECUTOR_H

#include "tool_registry.h"
#include <stdbool.h>
#include <stddef.h>

// Forward declarations for AI modules
typedef struct SemanticSearch SemanticSearch;
typedef struct VisualSearch VisualSearch;
typedef struct GeminiClient GeminiClient;

// Tool executor
typedef struct ToolExecutor {
    ToolRegistry *registry;
    char current_dir[1024];
    // AI search contexts (optional)
    SemanticSearch *semantic_search;
    VisualSearch *visual_search;
    // Gemini client for image generation (optional)
    GeminiClient *gemini_client;
} ToolExecutor;

// Pending operation for confirmation
typedef struct PendingOperation {
    char tool_id[64];
    char tool_name[64];
    char description[512];
    char details[2048];
    char input_json[4096];
    int affected_count;
    bool requires_confirmation;
} PendingOperation;

// Create and destroy executor
ToolExecutor *tool_executor_create(ToolRegistry *registry);
void tool_executor_destroy(ToolExecutor *executor);

// Set current working directory (for relative paths)
void tool_executor_set_cwd(ToolExecutor *executor, const char *path);

// Set AI search contexts (optional - enables semantic_search and visual_search tools)
void tool_executor_set_semantic_search(ToolExecutor *executor, SemanticSearch *search);
void tool_executor_set_visual_search(ToolExecutor *executor, VisualSearch *search);

// Set Gemini client (optional - enables image_generate tool)
void tool_executor_set_gemini_client(ToolExecutor *executor, GeminiClient *client);

// Execute a tool directly
ToolResult tool_executor_execute(ToolExecutor *executor, const char *tool_name, const char *input_json);

// Prepare an operation for confirmation (returns description without executing)
bool tool_executor_prepare(ToolExecutor *executor, const char *tool_name,
                           const char *tool_id, const char *input_json,
                           PendingOperation *pending);

// Execute a prepared operation after confirmation
ToolResult tool_executor_confirm(ToolExecutor *executor, const PendingOperation *pending);

// Cancel a pending operation
void tool_executor_cancel(PendingOperation *pending);

// Get human-readable description of an operation
const char *tool_executor_describe_operation(const char *tool_name, const char *input_json,
                                              char *buffer, size_t buffer_size);

#endif // TOOL_EXECUTOR_H

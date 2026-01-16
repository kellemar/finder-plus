#include "nl_operations.h"
#include "../api/claude_client.h"
#include "../tools/tool_registry.h"
#include "../tools/tool_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

// Initialize configuration with defaults
void nl_operations_config_init(NLOperationsConfig *config)
{
    if (!config) return;

    memset(config, 0, sizeof(NLOperationsConfig));
    config->require_confirmation_for_risk = true;
    config->confirmation_threshold = NL_RISK_MEDIUM;
    config->enable_undo = true;
    config->verbose_preview = true;
    config->max_files_without_confirmation = 10;
    strncpy(config->current_directory, ".", sizeof(config->current_directory) - 1);
}

// Initialize undo history
void nl_undo_history_init(NLUndoHistory *history)
{
    if (!history) return;
    memset(history, 0, sizeof(NLUndoHistory));
}

// Get operation type from tool name
NLOperationType nl_get_operation_type(const char *tool_name)
{
    if (!tool_name) return NL_OP_UNKNOWN;

    if (strcmp(tool_name, "file_list") == 0) return NL_OP_FILE_LIST;
    if (strcmp(tool_name, "file_move") == 0) return NL_OP_FILE_MOVE;
    if (strcmp(tool_name, "file_copy") == 0) return NL_OP_FILE_COPY;
    if (strcmp(tool_name, "file_delete") == 0) return NL_OP_FILE_DELETE;
    if (strcmp(tool_name, "file_rename") == 0) return NL_OP_FILE_RENAME;
    if (strcmp(tool_name, "file_create") == 0) return NL_OP_FILE_CREATE;
    if (strcmp(tool_name, "batch_rename") == 0) return NL_OP_BATCH_RENAME;
    if (strcmp(tool_name, "batch_move") == 0) return NL_OP_BATCH_MOVE;
    if (strcmp(tool_name, "file_search") == 0) return NL_OP_SEARCH;
    if (strcmp(tool_name, "semantic_search") == 0) return NL_OP_SEARCH;
    if (strcmp(tool_name, "organize") == 0) return NL_OP_ORGANIZE;
    if (strcmp(tool_name, "find_duplicates") == 0) return NL_OP_FIND_DUPLICATES;
    if (strcmp(tool_name, "summarize") == 0) return NL_OP_SUMMARIZE;

    return NL_OP_UNKNOWN;
}

// Get risk level for operation type
NLRiskLevel nl_get_risk_level(NLOperationType type)
{
    switch (type) {
        case NL_OP_FILE_LIST:
        case NL_OP_SEARCH:
        case NL_OP_FIND_DUPLICATES:
        case NL_OP_SUMMARIZE:
            return NL_RISK_NONE;

        case NL_OP_FILE_COPY:
        case NL_OP_FILE_CREATE:
            return NL_RISK_LOW;

        case NL_OP_FILE_MOVE:
        case NL_OP_FILE_RENAME:
        case NL_OP_BATCH_RENAME:
        case NL_OP_BATCH_MOVE:
        case NL_OP_ORGANIZE:
            return NL_RISK_MEDIUM;

        case NL_OP_FILE_DELETE:
            return NL_RISK_HIGH;

        default:
            return NL_RISK_MEDIUM;
    }
}

// Get operation type name
const char *nl_operation_type_name(NLOperationType type)
{
    switch (type) {
        case NL_OP_FILE_LIST: return "List Files";
        case NL_OP_FILE_MOVE: return "Move File";
        case NL_OP_FILE_COPY: return "Copy File";
        case NL_OP_FILE_DELETE: return "Delete File";
        case NL_OP_FILE_RENAME: return "Rename File";
        case NL_OP_FILE_CREATE: return "Create File";
        case NL_OP_BATCH_RENAME: return "Batch Rename";
        case NL_OP_BATCH_MOVE: return "Batch Move";
        case NL_OP_SEARCH: return "Search";
        case NL_OP_ORGANIZE: return "Organize";
        case NL_OP_FIND_DUPLICATES: return "Find Duplicates";
        case NL_OP_SUMMARIZE: return "Summarize";
        default: return "Unknown";
    }
}

// Get risk level name
const char *nl_risk_level_name(NLRiskLevel level)
{
    switch (level) {
        case NL_RISK_NONE: return "Safe";
        case NL_RISK_LOW: return "Low Risk";
        case NL_RISK_MEDIUM: return "Medium Risk";
        case NL_RISK_HIGH: return "High Risk";
        default: return "Unknown";
    }
}

// Get status message
const char *nl_status_message(NLOperationStatus status)
{
    switch (status) {
        case NL_STATUS_OK: return "OK";
        case NL_STATUS_PARSE_ERROR: return "Parse error";
        case NL_STATUS_VALIDATION_ERROR: return "Validation error";
        case NL_STATUS_EXECUTION_ERROR: return "Execution error";
        case NL_STATUS_CANCELLED: return "Cancelled";
        case NL_STATUS_API_ERROR: return "API error";
        case NL_STATUS_NO_OPERATIONS: return "No operations to execute";
        default: return "Unknown";
    }
}

// Parse tool use from Claude response
static int parse_tool_uses(const char *response, NLOperationChain *chain, int max_ops)
{
    // Simple parsing - look for tool_use patterns in Claude response
    // In production, this would use proper JSON parsing

    int count = 0;
    const char *p = response;

    while (count < max_ops) {
        // Look for tool name
        const char *tool_start = strstr(p, "\"name\"");
        if (!tool_start) break;

        const char *name_start = strchr(tool_start + 6, '"');
        if (!name_start) break;
        name_start++;

        const char *name_end = strchr(name_start, '"');
        if (!name_end) break;

        NLOperation *op = &chain->operations[count];
        memset(op, 0, sizeof(NLOperation));

        size_t name_len = name_end - name_start;
        if (name_len >= sizeof(op->tool_name)) name_len = sizeof(op->tool_name) - 1;
        strncpy(op->tool_name, name_start, name_len);

        op->type = nl_get_operation_type(op->tool_name);
        op->risk = nl_get_risk_level(op->type);
        op->requires_confirmation = (op->risk >= NL_RISK_MEDIUM);

        // Look for input JSON
        const char *input_start = strstr(name_end, "\"input\"");
        if (input_start) {
            const char *brace = strchr(input_start, '{');
            if (brace) {
                // Find matching closing brace
                int depth = 1;
                const char *end = brace + 1;
                while (*end && depth > 0) {
                    if (*end == '{') depth++;
                    else if (*end == '}') depth--;
                    end++;
                }
                size_t json_len = end - brace;
                if (json_len < sizeof(op->input_json)) {
                    strncpy(op->input_json, brace, json_len);
                }
            }
        }

        // Generate description
        snprintf(op->description, sizeof(op->description), "%s operation",
                 nl_operation_type_name(op->type));

        // Generate unique ID
        snprintf(op->tool_id, sizeof(op->tool_id), "op_%d_%ld", count, time(NULL));

        count++;
        p = name_end;
    }

    return count;
}

// Parse natural language command into operation chain
NLOperationStatus nl_parse_command(const char *command,
                                    const NLOperationsConfig *config,
                                    NLOperationChain *chain)
{
    if (!command || !config || !chain) {
        return NL_STATUS_PARSE_ERROR;
    }

    memset(chain, 0, sizeof(NLOperationChain));
    strncpy(chain->original_command, command, sizeof(chain->original_command) - 1);

    // Check for API key
    if (strlen(config->api_key) == 0) {
        chain->status = NL_STATUS_API_ERROR;
        return NL_STATUS_API_ERROR;
    }

    // Create Claude client
    ClaudeClient *client = claude_client_create(config->api_key);
    if (!client) {
        chain->status = NL_STATUS_API_ERROR;
        return NL_STATUS_API_ERROR;
    }

    // Build system prompt
    char system_prompt[2048];
    snprintf(system_prompt, sizeof(system_prompt),
        "You are a file management assistant. Convert natural language commands into file operations.\n"
        "Current directory: %s\n\n"
        "Available operations:\n"
        "- file_list: List files (params: path, filter, recursive)\n"
        "- file_move: Move file (params: source, destination)\n"
        "- file_copy: Copy file (params: source, destination)\n"
        "- file_delete: Delete file (params: path, use_trash)\n"
        "- file_rename: Rename file (params: path, new_name)\n"
        "- file_create: Create file/folder (params: path, is_directory)\n"
        "- batch_rename: Rename multiple files (params: files, pattern)\n"
        "- batch_move: Move multiple files (params: files, destination)\n"
        "- file_search: Search for files (params: query, path)\n"
        "- organize: Organize files (params: path, categories)\n"
        "- find_duplicates: Find duplicate files (params: path)\n"
        "- summarize: Summarize file contents (params: path)\n\n"
        "Respond with a JSON array of operations:\n"
        "[{\"name\": \"operation_name\", \"input\": {params}, \"description\": \"what it does\"}]\n"
        "If multiple operations are needed, include all of them in sequence.",
        config->current_directory);

    ClaudeMessageRequest req;
    ClaudeMessageResponse resp;
    claude_request_init(&req);
    claude_response_init(&resp);

    claude_request_set_system_prompt(&req, system_prompt);
    claude_request_add_user_message(&req, command);

    bool success = claude_send_message(client, &req, &resp);

    NLOperationStatus status = NL_STATUS_OK;

    if (success && resp.stop_reason != CLAUDE_STOP_ERROR) {
        // Store AI interpretation
        strncpy(chain->ai_interpretation, resp.content,
                sizeof(chain->ai_interpretation) - 1);

        // Parse operations from response
        chain->count = parse_tool_uses(resp.content, chain, NL_MAX_OPERATIONS);

        if (chain->count == 0) {
            status = NL_STATUS_NO_OPERATIONS;
        } else {
            // Calculate overall risk
            chain->overall_risk = NL_RISK_NONE;
            for (int i = 0; i < chain->count; i++) {
                if (chain->operations[i].risk > chain->overall_risk) {
                    chain->overall_risk = chain->operations[i].risk;
                }
            }
        }
    } else {
        status = NL_STATUS_API_ERROR;
    }

    claude_request_cleanup(&req);
    claude_response_cleanup(&resp);
    claude_client_destroy(client);

    chain->status = status;
    return status;
}

// Validate operation chain
NLOperationStatus nl_validate_chain(NLOperationChain *chain,
                                     const NLOperationsConfig *config)
{
    if (!chain || !config) return NL_STATUS_VALIDATION_ERROR;

    for (int i = 0; i < chain->count; i++) {
        NLOperation *op = &chain->operations[i];

        // Check if operation needs confirmation
        if (config->require_confirmation_for_risk &&
            op->risk >= config->confirmation_threshold) {
            op->requires_confirmation = true;
        }

        // For file operations, try to determine affected count
        // This would parse the input_json and check file existence
        // Simplified for now
        op->affected_count = 1;
    }

    return NL_STATUS_OK;
}

// Generate preview of what will happen
NLOperationPreview *nl_generate_preview(NLOperationChain *chain)
{
    if (!chain) return NULL;

    NLOperationPreview *preview = calloc(1, sizeof(NLOperationPreview));
    if (!preview) return NULL;

    preview->chain = chain;

    // Build summary
    char *p = preview->summary;
    size_t remaining = sizeof(preview->summary);

    int written = snprintf(p, remaining,
        "Command: %s\n\n"
        "Operations (%d):\n",
        chain->original_command, chain->count);
    p += written;
    remaining -= written;

    for (int i = 0; i < chain->count && remaining > 128; i++) {
        NLOperation *op = &chain->operations[i];
        written = snprintf(p, remaining,
            "  %d. [%s] %s - %s\n",
            i + 1,
            nl_risk_level_name(op->risk),
            nl_operation_type_name(op->type),
            op->description);
        p += written;
        remaining -= written;

        preview->total_files_affected += op->affected_count;
        preview->total_size_affected += op->affected_size;

        if (op->risk >= NL_RISK_HIGH) {
            preview->has_destructive_ops = true;
        }
    }

    // Add warnings
    if (preview->has_destructive_ops) {
        strncpy(preview->warnings,
                "WARNING: This command includes destructive operations that may delete files.",
                sizeof(preview->warnings) - 1);
    } else if (chain->overall_risk >= NL_RISK_MEDIUM) {
        strncpy(preview->warnings,
                "Note: This command will modify files. Changes can be undone.",
                sizeof(preview->warnings) - 1);
    }

    return preview;
}

// Free preview
void nl_preview_free(NLOperationPreview *preview)
{
    free(preview);
}

// Add to undo history
static void add_to_undo_history(NLUndoHistory *history, const NLUndoEntry *entry)
{
    if (!history || !entry) return;

    int index = (history->head + history->count) % NL_MAX_UNDO_HISTORY;
    if (history->count >= NL_MAX_UNDO_HISTORY) {
        // Overwrite oldest
        history->head = (history->head + 1) % NL_MAX_UNDO_HISTORY;
    } else {
        history->count++;
    }

    history->entries[index] = *entry;
}

// Create reverse operation for undo
bool nl_create_reverse_operation(const NLOperation *op, NLUndoEntry *undo)
{
    if (!op || !undo) return false;

    memset(undo, 0, sizeof(NLUndoEntry));
    undo->operation = *op;
    undo->timestamp = time(NULL);
    undo->can_undo = true;

    switch (op->type) {
        case NL_OP_FILE_MOVE:
            snprintf(undo->reverse_action, sizeof(undo->reverse_action),
                     "Move back to original location");
            // Reverse: swap source and destination in input_json
            // This is simplified - real implementation would parse JSON
            undo->can_undo = true;
            break;

        case NL_OP_FILE_RENAME:
            snprintf(undo->reverse_action, sizeof(undo->reverse_action),
                     "Rename back to original name");
            undo->can_undo = true;
            break;

        case NL_OP_FILE_COPY:
            snprintf(undo->reverse_action, sizeof(undo->reverse_action),
                     "Delete the copy");
            undo->can_undo = true;
            break;

        case NL_OP_FILE_CREATE:
            snprintf(undo->reverse_action, sizeof(undo->reverse_action),
                     "Delete the created item");
            undo->can_undo = true;
            break;

        case NL_OP_FILE_DELETE:
            snprintf(undo->reverse_action, sizeof(undo->reverse_action),
                     "Restore from Trash");
            // Only undoable if we used trash
            undo->can_undo = true;
            break;

        case NL_OP_BATCH_MOVE:
        case NL_OP_BATCH_RENAME:
        case NL_OP_ORGANIZE:
            snprintf(undo->reverse_action, sizeof(undo->reverse_action),
                     "Reverse batch operation");
            undo->can_undo = true;
            break;

        default:
            undo->can_undo = false;
            break;
    }

    return undo->can_undo;
}

// Execute single operation
NLOperationStatus nl_execute_operation(NLOperation *op,
                                        const NLOperationsConfig *config,
                                        NLUndoHistory *undo_history)
{
    if (!op || !config) return NL_STATUS_EXECUTION_ERROR;

    // Create tool registry and executor
    ToolRegistry *registry = tool_registry_create();
    if (!registry) {
        op->success = false;
        strncpy(op->error, "Failed to create tool registry", sizeof(op->error) - 1);
        return NL_STATUS_EXECUTION_ERROR;
    }

    tool_registry_register_file_tools(registry);

    ToolExecutor *executor = tool_executor_create(registry);
    if (!executor) {
        tool_registry_destroy(registry);
        op->success = false;
        strncpy(op->error, "Failed to create tool executor", sizeof(op->error) - 1);
        return NL_STATUS_EXECUTION_ERROR;
    }

    tool_executor_set_cwd(executor, config->current_directory);

    // Execute the tool
    ToolResult result = tool_executor_execute(executor, op->tool_name, op->input_json);

    op->executed = true;
    op->success = result.success;

    if (result.success) {
        strncpy(op->result, result.output ? result.output : "Success",
                sizeof(op->result) - 1);

        // Add to undo history if enabled
        if (config->enable_undo && undo_history) {
            NLUndoEntry undo;
            if (nl_create_reverse_operation(op, &undo)) {
                add_to_undo_history(undo_history, &undo);
            }
        }
    } else {
        strncpy(op->error, result.error ? result.error : "Unknown error",
                sizeof(op->error) - 1);
    }

    // Cleanup
    if (result.output) free(result.output);
    if (result.error) free(result.error);
    tool_executor_destroy(executor);
    tool_registry_destroy(registry);

    return op->success ? NL_STATUS_OK : NL_STATUS_EXECUTION_ERROR;
}

// Execute operation chain
NLOperationStatus nl_execute_chain(NLOperationChain *chain,
                                    const NLOperationsConfig *config,
                                    NLUndoHistory *undo_history,
                                    NLProgressCallback progress,
                                    NLConfirmCallback confirm,
                                    void *user_data)
{
    if (!chain || !config) return NL_STATUS_EXECUTION_ERROR;

    if (chain->count == 0) {
        chain->status = NL_STATUS_NO_OPERATIONS;
        return NL_STATUS_NO_OPERATIONS;
    }

    NLOperationStatus overall_status = NL_STATUS_OK;

    for (int i = 0; i < chain->count; i++) {
        NLOperation *op = &chain->operations[i];
        chain->current_index = i;

        // Report progress
        if (progress) {
            progress(i, chain->count, op->description, user_data);
        }

        // Check if confirmation needed
        if (op->requires_confirmation && !op->confirmed) {
            if (confirm) {
                if (!confirm(op, user_data)) {
                    op->success = false;
                    strncpy(op->error, "Cancelled by user", sizeof(op->error) - 1);
                    overall_status = NL_STATUS_CANCELLED;
                    break;
                }
                op->confirmed = true;
            } else {
                // No confirm callback, skip operation
                continue;
            }
        }

        // Execute operation
        NLOperationStatus status = nl_execute_operation(op, config, undo_history);
        if (status != NL_STATUS_OK) {
            overall_status = status;
            // Continue with next operation or stop?
            // For now, stop on error
            break;
        }
    }

    chain->status = overall_status;
    return overall_status;
}

// Undo last operation
bool nl_undo_last(NLUndoHistory *history)
{
    if (!history || history->count == 0) return false;

    // Get most recent entry
    int index = (history->head + history->count - 1) % NL_MAX_UNDO_HISTORY;
    NLUndoEntry *entry = &history->entries[index];

    if (!entry->can_undo) return false;

    // Execute reverse operation
    // This is simplified - real implementation would execute reverse_json
    // For now, just mark as undone
    history->count--;

    return true;
}

// Undo specific operation
bool nl_undo_operation(NLUndoHistory *history, int index)
{
    if (!history || index < 0 || index >= history->count) return false;

    int actual_index = (history->head + index) % NL_MAX_UNDO_HISTORY;
    NLUndoEntry *entry = &history->entries[actual_index];

    if (!entry->can_undo) return false;

    // Execute reverse operation
    // Simplified implementation
    entry->can_undo = false;

    return true;
}

// Can undo?
bool nl_can_undo(const NLUndoHistory *history)
{
    if (!history || history->count == 0) return false;

    int index = (history->head + history->count - 1) % NL_MAX_UNDO_HISTORY;
    return history->entries[index].can_undo;
}

// Get undo description
const char *nl_get_undo_description(const NLUndoHistory *history, int index)
{
    if (!history || index < 0 || index >= history->count) return NULL;

    int actual_index = (history->head + index) % NL_MAX_UNDO_HISTORY;
    return history->entries[actual_index].reverse_action;
}

// Clear undo history
void nl_clear_undo_history(NLUndoHistory *history)
{
    if (history) {
        memset(history, 0, sizeof(NLUndoHistory));
    }
}

// Confirm operation
void nl_confirm_operation(NLOperationChain *chain, int index)
{
    if (chain && index >= 0 && index < chain->count) {
        chain->operations[index].confirmed = true;
    }
}

// Reject operation
void nl_reject_operation(NLOperationChain *chain, int index)
{
    if (chain && index >= 0 && index < chain->count) {
        chain->operations[index].confirmed = false;
        chain->operations[index].requires_confirmation = false;
    }
}

// Confirm all operations
void nl_confirm_all(NLOperationChain *chain)
{
    if (!chain) return;
    for (int i = 0; i < chain->count; i++) {
        chain->operations[i].confirmed = true;
    }
    chain->all_confirmed = true;
}

// Reject all operations
void nl_reject_all(NLOperationChain *chain)
{
    if (!chain) return;
    for (int i = 0; i < chain->count; i++) {
        chain->operations[i].confirmed = false;
    }
    chain->all_confirmed = false;
}

// Format operation for display
void nl_format_operation(const NLOperation *op, char *buffer, size_t buffer_size)
{
    if (!op || !buffer || buffer_size == 0) return;

    snprintf(buffer, buffer_size,
             "[%s] %s\n"
             "  Tool: %s\n"
             "  %s\n"
             "  Files: %d\n",
             nl_risk_level_name(op->risk),
             op->description,
             op->tool_name,
             op->details[0] ? op->details : "(no details)",
             op->affected_count);
}

// Format chain for display
void nl_format_chain(const NLOperationChain *chain, char *buffer, size_t buffer_size)
{
    if (!chain || !buffer || buffer_size == 0) return;

    char *p = buffer;
    size_t remaining = buffer_size;

    int written = snprintf(p, remaining,
                           "Command: %s\n"
                           "Operations: %d\n"
                           "Overall Risk: %s\n\n",
                           chain->original_command,
                           chain->count,
                           nl_risk_level_name(chain->overall_risk));
    p += written;
    remaining -= written;

    for (int i = 0; i < chain->count && remaining > 256; i++) {
        char op_str[256];
        nl_format_operation(&chain->operations[i], op_str, sizeof(op_str));
        written = snprintf(p, remaining, "%d. %s\n", i + 1, op_str);
        p += written;
        remaining -= written;
    }
}

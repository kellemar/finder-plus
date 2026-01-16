#ifndef NL_OPERATIONS_H
#define NL_OPERATIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Maximum operations in a chain
#define NL_MAX_OPERATIONS 16

// Maximum undo history
#define NL_MAX_UNDO_HISTORY 32

// Operation status
typedef enum NLOperationStatus {
    NL_STATUS_OK = 0,
    NL_STATUS_PARSE_ERROR,
    NL_STATUS_VALIDATION_ERROR,
    NL_STATUS_EXECUTION_ERROR,
    NL_STATUS_CANCELLED,
    NL_STATUS_API_ERROR,
    NL_STATUS_NO_OPERATIONS
} NLOperationStatus;

// Operation type
typedef enum NLOperationType {
    NL_OP_FILE_LIST,
    NL_OP_FILE_MOVE,
    NL_OP_FILE_COPY,
    NL_OP_FILE_DELETE,
    NL_OP_FILE_RENAME,
    NL_OP_FILE_CREATE,
    NL_OP_BATCH_RENAME,
    NL_OP_BATCH_MOVE,
    NL_OP_SEARCH,
    NL_OP_ORGANIZE,
    NL_OP_FIND_DUPLICATES,
    NL_OP_SUMMARIZE,
    NL_OP_UNKNOWN
} NLOperationType;

// Operation risk level
typedef enum NLRiskLevel {
    NL_RISK_NONE,       // Read-only operations
    NL_RISK_LOW,        // Copy, create (non-destructive)
    NL_RISK_MEDIUM,     // Move, rename (reversible)
    NL_RISK_HIGH        // Delete (may lose data)
} NLRiskLevel;

// Single operation in a chain
typedef struct NLOperation {
    NLOperationType type;
    char tool_name[64];
    char tool_id[64];
    char description[256];
    char details[1024];
    char input_json[4096];
    NLRiskLevel risk;
    int affected_count;
    uint64_t affected_size;
    bool requires_confirmation;
    bool confirmed;
    bool executed;
    bool success;
    char result[1024];
    char error[256];
} NLOperation;

// Operation chain (sequence of operations)
typedef struct NLOperationChain {
    NLOperation operations[NL_MAX_OPERATIONS];
    int count;
    int current_index;      // Current operation being executed
    char original_command[512];
    char ai_interpretation[1024];
    NLRiskLevel overall_risk;
    bool preview_shown;
    bool all_confirmed;
    NLOperationStatus status;
} NLOperationChain;

// Undo entry
typedef struct NLUndoEntry {
    NLOperation operation;
    char reverse_action[256];
    char reverse_json[4096];
    time_t timestamp;
    bool can_undo;
} NLUndoEntry;

// Undo history
typedef struct NLUndoHistory {
    NLUndoEntry entries[NL_MAX_UNDO_HISTORY];
    int count;
    int head;               // Circular buffer head
} NLUndoHistory;

// Operation preview
typedef struct NLOperationPreview {
    NLOperationChain *chain;
    char summary[2048];
    char warnings[1024];
    int total_files_affected;
    uint64_t total_size_affected;
    bool has_destructive_ops;
} NLOperationPreview;

// Configuration
typedef struct NLOperationsConfig {
    char api_key[256];
    bool require_confirmation_for_risk;
    NLRiskLevel confirmation_threshold;
    bool enable_undo;
    bool verbose_preview;
    int max_files_without_confirmation;
    char current_directory[1024];
} NLOperationsConfig;

// Progress callback
typedef void (*NLProgressCallback)(int op_index, int total_ops, const char *description, void *user_data);

// Confirmation callback (returns true if user confirms)
typedef bool (*NLConfirmCallback)(const NLOperation *op, void *user_data);

// Initialize configuration with defaults
void nl_operations_config_init(NLOperationsConfig *config);

// Initialize undo history
void nl_undo_history_init(NLUndoHistory *history);

// Parse natural language command into operation chain
NLOperationStatus nl_parse_command(const char *command,
                                    const NLOperationsConfig *config,
                                    NLOperationChain *chain);

// Validate operation chain (check files exist, permissions, etc.)
NLOperationStatus nl_validate_chain(NLOperationChain *chain,
                                     const NLOperationsConfig *config);

// Generate preview of what will happen
NLOperationPreview *nl_generate_preview(NLOperationChain *chain);

// Free preview
void nl_preview_free(NLOperationPreview *preview);

// Execute operation chain
NLOperationStatus nl_execute_chain(NLOperationChain *chain,
                                    const NLOperationsConfig *config,
                                    NLUndoHistory *undo_history,
                                    NLProgressCallback progress,
                                    NLConfirmCallback confirm,
                                    void *user_data);

// Execute single operation
NLOperationStatus nl_execute_operation(NLOperation *op,
                                        const NLOperationsConfig *config,
                                        NLUndoHistory *undo_history);

// Undo last operation
bool nl_undo_last(NLUndoHistory *history);

// Undo specific operation (by index from head)
bool nl_undo_operation(NLUndoHistory *history, int index);

// Can undo?
bool nl_can_undo(const NLUndoHistory *history);

// Get undo description
const char *nl_get_undo_description(const NLUndoHistory *history, int index);

// Clear undo history
void nl_clear_undo_history(NLUndoHistory *history);

// Confirm/reject operations
void nl_confirm_operation(NLOperationChain *chain, int index);
void nl_reject_operation(NLOperationChain *chain, int index);
void nl_confirm_all(NLOperationChain *chain);
void nl_reject_all(NLOperationChain *chain);

// Get operation type from tool name
NLOperationType nl_get_operation_type(const char *tool_name);

// Get risk level for operation type
NLRiskLevel nl_get_risk_level(NLOperationType type);

// Get operation type name
const char *nl_operation_type_name(NLOperationType type);

// Get risk level name
const char *nl_risk_level_name(NLRiskLevel level);

// Get status message
const char *nl_status_message(NLOperationStatus status);

// Format operation for display
void nl_format_operation(const NLOperation *op, char *buffer, size_t buffer_size);

// Format chain for display
void nl_format_chain(const NLOperationChain *chain, char *buffer, size_t buffer_size);

// Create reverse operation for undo
bool nl_create_reverse_operation(const NLOperation *op, NLUndoEntry *undo);

#endif // NL_OPERATIONS_H

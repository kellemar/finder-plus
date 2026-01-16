#ifndef CLAUDE_CLIENT_H
#define CLAUDE_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#define CLAUDE_MAX_API_KEY_LEN 256
#define CLAUDE_MAX_MODEL_LEN 64
#define CLAUDE_MAX_SYSTEM_PROMPT_LEN 8192
#define CLAUDE_MAX_MESSAGE_LEN 32768
#define CLAUDE_MAX_RESPONSE_LEN 65536
#define CLAUDE_MAX_TOOL_NAME_LEN 64
#define CLAUDE_MAX_TOOLS 16

#define CLAUDE_API_URL "https://api.anthropic.com/v1/messages"
#define CLAUDE_API_VERSION "2023-06-01"
#define CLAUDE_DEFAULT_MODEL "claude-haiku-4-5-20251001"
#define CLAUDE_DEFAULT_MAX_TOKENS 4096

// Stop reasons
typedef enum ClaudeStopReason {
    CLAUDE_STOP_END_TURN = 0,
    CLAUDE_STOP_MAX_TOKENS,
    CLAUDE_STOP_TOOL_USE,
    CLAUDE_STOP_ERROR
} ClaudeStopReason;

// Tool use from response
typedef struct ClaudeToolUse {
    char id[64];
    char name[CLAUDE_MAX_TOOL_NAME_LEN];
    char input[CLAUDE_MAX_MESSAGE_LEN];
} ClaudeToolUse;

// Conversation message
typedef struct ClaudeMessage {
    char role[16];
    char content[CLAUDE_MAX_MESSAGE_LEN];
    ClaudeToolUse *tool_uses;
    int tool_use_count;
} ClaudeMessage;

// Message request
typedef struct ClaudeMessageRequest {
    char model[CLAUDE_MAX_MODEL_LEN];
    int max_tokens;
    char system_prompt[CLAUDE_MAX_SYSTEM_PROMPT_LEN];
    ClaudeMessage *messages;
    int message_count;
    int message_capacity;
    struct cJSON *tools;
} ClaudeMessageRequest;

// Message response
typedef struct ClaudeMessageResponse {
    char id[64];
    char content[CLAUDE_MAX_RESPONSE_LEN];
    ClaudeStopReason stop_reason;
    int input_tokens;
    int output_tokens;
    ClaudeToolUse *tool_uses;
    int tool_use_count;
    char *error;
} ClaudeMessageResponse;

// Client
typedef struct ClaudeClient {
    char api_key[CLAUDE_MAX_API_KEY_LEN];
    bool initialized;
} ClaudeClient;

// Client lifecycle
ClaudeClient *claude_client_create(const char *api_key);
void claude_client_destroy(ClaudeClient *client);
bool claude_client_is_valid(const ClaudeClient *client);

// Request functions
void claude_request_init(ClaudeMessageRequest *req);
void claude_request_cleanup(ClaudeMessageRequest *req);
void claude_request_set_model(ClaudeMessageRequest *req, const char *model);
void claude_request_set_max_tokens(ClaudeMessageRequest *req, int max_tokens);
void claude_request_set_system_prompt(ClaudeMessageRequest *req, const char *prompt);
void claude_request_add_user_message(ClaudeMessageRequest *req, const char *content);
void claude_request_add_assistant_message(ClaudeMessageRequest *req, const char *content);
void claude_request_add_tool_result(ClaudeMessageRequest *req, const char *tool_id, const char *result);
void claude_request_set_tools(ClaudeMessageRequest *req, struct cJSON *tools);

// Response functions
void claude_response_init(ClaudeMessageResponse *resp);
void claude_response_cleanup(ClaudeMessageResponse *resp);

// Send message
bool claude_send_message(ClaudeClient *client, const ClaudeMessageRequest *req, ClaudeMessageResponse *resp);

// Parse response JSON
bool claude_parse_response(const char *json, ClaudeMessageResponse *resp);

// Utility
const char *claude_stop_reason_to_string(ClaudeStopReason reason);
ClaudeStopReason claude_stop_reason_from_string(const char *str);
bool claude_response_has_tool_use(const ClaudeMessageResponse *resp);

#endif // CLAUDE_CLIENT_H

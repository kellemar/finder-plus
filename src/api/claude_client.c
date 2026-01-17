#include "claude_client.h"
#include "http_client.h"
#include "../../external/cJSON/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

ClaudeClient *claude_client_create(const char *api_key)
{
    if (!api_key || strlen(api_key) == 0) return NULL;

    ClaudeClient *client = (ClaudeClient *)calloc(1, sizeof(ClaudeClient));
    if (!client) return NULL;

    strncpy(client->api_key, api_key, CLAUDE_MAX_API_KEY_LEN - 1);
    client->api_key[CLAUDE_MAX_API_KEY_LEN - 1] = '\0';
    client->initialized = true;

    return client;
}

void claude_client_destroy(ClaudeClient *client)
{
    if (client) {
        memset(client->api_key, 0, CLAUDE_MAX_API_KEY_LEN);
        free(client);
    }
}

bool claude_client_is_valid(const ClaudeClient *client)
{
    return client && client->initialized && strlen(client->api_key) > 0;
}

void claude_request_init(ClaudeMessageRequest *req)
{
    if (!req) return;
    memset(req, 0, sizeof(ClaudeMessageRequest));
    strncpy(req->model, CLAUDE_DEFAULT_MODEL, CLAUDE_MAX_MODEL_LEN - 1);
    req->max_tokens = CLAUDE_DEFAULT_MAX_TOKENS;
}

void claude_request_cleanup(ClaudeMessageRequest *req)
{
    if (!req) return;
    if (req->messages) {
        for (int i = 0; i < req->message_count; i++) {
            if (req->messages[i].tool_uses) {
                free(req->messages[i].tool_uses);
            }
        }
        free(req->messages);
        req->messages = NULL;
    }
    req->message_count = 0;
    req->message_capacity = 0;
}

void claude_request_set_model(ClaudeMessageRequest *req, const char *model)
{
    if (!req || !model) return;
    strncpy(req->model, model, CLAUDE_MAX_MODEL_LEN - 1);
    req->model[CLAUDE_MAX_MODEL_LEN - 1] = '\0';
}

void claude_request_set_max_tokens(ClaudeMessageRequest *req, int max_tokens)
{
    if (!req) return;
    req->max_tokens = max_tokens > 0 ? max_tokens : CLAUDE_DEFAULT_MAX_TOKENS;
}

void claude_request_set_system_prompt(ClaudeMessageRequest *req, const char *prompt)
{
    if (!req || !prompt) return;
    strncpy(req->system_prompt, prompt, CLAUDE_MAX_SYSTEM_PROMPT_LEN - 1);
    req->system_prompt[CLAUDE_MAX_SYSTEM_PROMPT_LEN - 1] = '\0';
}

static void ensure_message_capacity(ClaudeMessageRequest *req)
{
    if (!req) return;
    if (req->message_count >= req->message_capacity) {
        int new_capacity = req->message_capacity == 0 ? 8 : req->message_capacity * 2;
        ClaudeMessage *new_messages = (ClaudeMessage *)realloc(req->messages, new_capacity * sizeof(ClaudeMessage));
        if (new_messages) {
            req->messages = new_messages;
            req->message_capacity = new_capacity;
        }
    }
}

void claude_request_add_user_message(ClaudeMessageRequest *req, const char *content)
{
    if (!req || !content) return;
    ensure_message_capacity(req);
    if (req->message_count >= req->message_capacity) return;

    ClaudeMessage *msg = &req->messages[req->message_count];
    memset(msg, 0, sizeof(ClaudeMessage));
    strncpy(msg->role, "user", 15);
    strncpy(msg->content, content, CLAUDE_MAX_MESSAGE_LEN - 1);
    req->message_count++;
}

void claude_request_add_assistant_message(ClaudeMessageRequest *req, const char *content)
{
    if (!req || !content) return;
    ensure_message_capacity(req);
    if (req->message_count >= req->message_capacity) return;

    ClaudeMessage *msg = &req->messages[req->message_count];
    memset(msg, 0, sizeof(ClaudeMessage));
    strncpy(msg->role, "assistant", 15);
    strncpy(msg->content, content, CLAUDE_MAX_MESSAGE_LEN - 1);
    req->message_count++;
}

void claude_request_add_tool_result(ClaudeMessageRequest *req, const char *tool_id, const char *result)
{
    if (!req || !tool_id || !result) return;
    ensure_message_capacity(req);
    if (req->message_count >= req->message_capacity) return;

    ClaudeMessage *msg = &req->messages[req->message_count];
    memset(msg, 0, sizeof(ClaudeMessage));
    strncpy(msg->role, "user", 15);

    // Format as tool result content
    snprintf(msg->content, CLAUDE_MAX_MESSAGE_LEN - 1,
             "{\"type\":\"tool_result\",\"tool_use_id\":\"%s\",\"content\":\"%s\"}",
             tool_id, result);
    req->message_count++;
}

void claude_request_set_tools(ClaudeMessageRequest *req, cJSON *tools)
{
    if (!req) return;
    req->tools = tools;
}

static cJSON *build_message_request_json(const ClaudeMessageRequest *req)
{
    if (!req) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", req->model);
    cJSON_AddNumberToObject(root, "max_tokens", req->max_tokens);

    if (req->system_prompt[0] != '\0') {
        cJSON_AddStringToObject(root, "system", req->system_prompt);
    }

    cJSON *messages = cJSON_CreateArray();
    if (messages) {
        for (int i = 0; i < req->message_count; i++) {
            cJSON *msg = cJSON_CreateObject();
            if (msg) {
                cJSON_AddStringToObject(msg, "role", req->messages[i].role);

                // Check if this is a tool result message
                if (strstr(req->messages[i].content, "\"type\":\"tool_result\"")) {
                    cJSON *content = cJSON_Parse(req->messages[i].content);
                    if (content) {
                        cJSON *content_array = cJSON_CreateArray();
                        cJSON_AddItemToArray(content_array, content);
                        cJSON_AddItemToObject(msg, "content", content_array);
                    } else {
                        cJSON_AddStringToObject(msg, "content", req->messages[i].content);
                    }
                } else {
                    cJSON_AddStringToObject(msg, "content", req->messages[i].content);
                }

                cJSON_AddItemToArray(messages, msg);
            }
        }
        cJSON_AddItemToObject(root, "messages", messages);
    }

    if (req->tools) {
        cJSON *tools_copy = cJSON_Duplicate(req->tools, 1);
        if (tools_copy) {
            cJSON_AddItemToObject(root, "tools", tools_copy);
        }
    }

    return root;
}

void claude_response_init(ClaudeMessageResponse *resp)
{
    if (!resp) return;
    memset(resp, 0, sizeof(ClaudeMessageResponse));
}

void claude_response_cleanup(ClaudeMessageResponse *resp)
{
    if (!resp) return;
    if (resp->tool_uses) {
        free(resp->tool_uses);
        resp->tool_uses = NULL;
    }
    if (resp->error) {
        free(resp->error);
        resp->error = NULL;
    }
    resp->tool_use_count = 0;
}

bool claude_parse_response(const char *json, ClaudeMessageResponse *resp)
{
    if (!json || !resp) return false;

    claude_response_init(resp);

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        resp->error = strdup("Failed to parse JSON response");
        resp->stop_reason = CLAUDE_STOP_ERROR;
        return false;
    }

    // Check for API error
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *error_msg = cJSON_GetObjectItem(error, "message");
        if (error_msg && cJSON_IsString(error_msg)) {
            resp->error = strdup(error_msg->valuestring);
        } else {
            resp->error = strdup("Unknown API error");
        }
        resp->stop_reason = CLAUDE_STOP_ERROR;
        cJSON_Delete(root);
        return false;
    }

    // Parse response ID
    cJSON *id = cJSON_GetObjectItem(root, "id");
    if (id && cJSON_IsString(id)) {
        strncpy(resp->id, id->valuestring, 63);
        resp->id[63] = '\0';
    }

    // Parse stop reason
    cJSON *stop_reason = cJSON_GetObjectItem(root, "stop_reason");
    if (stop_reason && cJSON_IsString(stop_reason)) {
        resp->stop_reason = claude_stop_reason_from_string(stop_reason->valuestring);
    }

    // Parse usage
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *input_tokens = cJSON_GetObjectItem(usage, "input_tokens");
        cJSON *output_tokens = cJSON_GetObjectItem(usage, "output_tokens");
        if (input_tokens && cJSON_IsNumber(input_tokens)) {
            resp->input_tokens = input_tokens->valueint;
        }
        if (output_tokens && cJSON_IsNumber(output_tokens)) {
            resp->output_tokens = output_tokens->valueint;
        }
    }

    // Parse content blocks
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (content && cJSON_IsArray(content)) {
        int tool_use_count = 0;
        int array_size = cJSON_GetArraySize(content);

        // Count tool uses first
        for (int i = 0; i < array_size; i++) {
            cJSON *block = cJSON_GetArrayItem(content, i);
            cJSON *type = cJSON_GetObjectItem(block, "type");
            if (type && cJSON_IsString(type) && strcmp(type->valuestring, "tool_use") == 0) {
                tool_use_count++;
            }
        }

        if (tool_use_count > 0) {
            resp->tool_uses = (ClaudeToolUse *)calloc((size_t)tool_use_count, sizeof(ClaudeToolUse));
            if (!resp->tool_uses) {
                resp->error = strdup("Memory allocation failed for tool uses");
                resp->stop_reason = CLAUDE_STOP_ERROR;
                cJSON_Delete(root);
                return false;
            }
            resp->tool_use_count = tool_use_count;
        }

        int tool_idx = 0;
        for (int i = 0; i < array_size; i++) {
            cJSON *block = cJSON_GetArrayItem(content, i);
            cJSON *type = cJSON_GetObjectItem(block, "type");

            if (!type || !cJSON_IsString(type)) continue;

            if (strcmp(type->valuestring, "text") == 0) {
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (text && cJSON_IsString(text)) {
                    size_t current_len = strlen(resp->content);
                    size_t available = CLAUDE_MAX_RESPONSE_LEN - current_len - 1;
                    if (available > 0) {
                        strncat(resp->content, text->valuestring, available);
                        resp->content[CLAUDE_MAX_RESPONSE_LEN - 1] = '\0';
                    }
                }
            } else if (strcmp(type->valuestring, "tool_use") == 0 && resp->tool_uses && tool_idx < tool_use_count) {
                cJSON *tool_id = cJSON_GetObjectItem(block, "id");
                cJSON *tool_name = cJSON_GetObjectItem(block, "name");
                cJSON *tool_input = cJSON_GetObjectItem(block, "input");

                if (tool_id && cJSON_IsString(tool_id)) {
                    strncpy(resp->tool_uses[tool_idx].id, tool_id->valuestring, 63);
                    resp->tool_uses[tool_idx].id[63] = '\0';
                }
                if (tool_name && cJSON_IsString(tool_name)) {
                    strncpy(resp->tool_uses[tool_idx].name, tool_name->valuestring, CLAUDE_MAX_TOOL_NAME_LEN - 1);
                    resp->tool_uses[tool_idx].name[CLAUDE_MAX_TOOL_NAME_LEN - 1] = '\0';
                }
                if (tool_input) {
                    char *input_str = cJSON_PrintUnformatted(tool_input);
                    if (input_str) {
                        strncpy(resp->tool_uses[tool_idx].input, input_str, CLAUDE_MAX_MESSAGE_LEN - 1);
                        resp->tool_uses[tool_idx].input[CLAUDE_MAX_MESSAGE_LEN - 1] = '\0';
                        free(input_str);
                    }
                }
                tool_idx++;
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

const char *claude_stop_reason_to_string(ClaudeStopReason reason)
{
    switch (reason) {
        case CLAUDE_STOP_END_TURN: return "end_turn";
        case CLAUDE_STOP_MAX_TOKENS: return "max_tokens";
        case CLAUDE_STOP_TOOL_USE: return "tool_use";
        case CLAUDE_STOP_ERROR: return "error";
        default: return "unknown";
    }
}

ClaudeStopReason claude_stop_reason_from_string(const char *str)
{
    if (!str) return CLAUDE_STOP_ERROR;
    if (strcmp(str, "end_turn") == 0) return CLAUDE_STOP_END_TURN;
    if (strcmp(str, "max_tokens") == 0) return CLAUDE_STOP_MAX_TOKENS;
    if (strcmp(str, "tool_use") == 0) return CLAUDE_STOP_TOOL_USE;
    return CLAUDE_STOP_ERROR;
}

bool claude_response_has_tool_use(const ClaudeMessageResponse *resp)
{
    return resp && resp->tool_use_count > 0 && resp->tool_uses != NULL;
}

bool claude_send_message(ClaudeClient *client, const ClaudeMessageRequest *req, ClaudeMessageResponse *resp)
{
    if (!client || !req || !resp) return false;
    if (!claude_client_is_valid(client)) {
        resp->error = strdup("Claude client not initialized or invalid API key");
        resp->stop_reason = CLAUDE_STOP_ERROR;
        return false;
    }

    HttpClient *http_client = http_client_create();
    if (!http_client) {
        resp->error = strdup("Failed to create HTTP client");
        resp->stop_reason = CLAUDE_STOP_ERROR;
        return false;
    }

    cJSON *request_json = build_message_request_json(req);
    if (!request_json) {
        http_client_destroy(http_client);
        resp->error = strdup("Failed to build request JSON");
        resp->stop_reason = CLAUDE_STOP_ERROR;
        return false;
    }

    char *request_body = cJSON_PrintUnformatted(request_json);
    cJSON_Delete(request_json);

    if (!request_body) {
        http_client_destroy(http_client);
        resp->error = strdup("Failed to serialize request JSON");
        resp->stop_reason = CLAUDE_STOP_ERROR;
        return false;
    }

    HttpRequest http_req;
    http_request_init(&http_req);
    http_request_set_method(&http_req, HTTP_POST);
    http_request_set_url(&http_req, CLAUDE_API_URL);
    http_request_add_header(&http_req, "Content-Type", "application/json");
    http_request_add_header(&http_req, "x-api-key", client->api_key);
    http_request_add_header(&http_req, "anthropic-version", CLAUDE_API_VERSION);
    http_request_set_body_string(&http_req, request_body);

    free(request_body);

    HttpResponse http_resp;
    http_response_init(&http_resp);

    bool success = http_client_execute(http_client, &http_req, &http_resp);
    http_request_cleanup(&http_req);
    http_client_destroy(http_client);

    if (!success) {
        resp->error = http_resp.error ? strdup(http_resp.error) : strdup("HTTP request failed");
        resp->stop_reason = CLAUDE_STOP_ERROR;
        http_response_cleanup(&http_resp);
        return false;
    }

    if (http_resp.status_code != 200) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "API returned status %d", http_resp.status_code);

        if (http_resp.body && http_resp.body_len > 0) {
            claude_parse_response(http_resp.body, resp);
            if (!resp->error) {
                resp->error = strdup(err_buf);
            }
        } else {
            resp->error = strdup(err_buf);
        }
        resp->stop_reason = CLAUDE_STOP_ERROR;
        http_response_cleanup(&http_resp);
        return false;
    }

    if (!http_resp.body || http_resp.body_len == 0) {
        resp->error = strdup("Empty response from API");
        resp->stop_reason = CLAUDE_STOP_ERROR;
        http_response_cleanup(&http_resp);
        return false;
    }

    success = claude_parse_response(http_resp.body, resp);
    http_response_cleanup(&http_resp);

    return success;
}

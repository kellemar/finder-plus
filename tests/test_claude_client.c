#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api/claude_client.h"
#include "api/auth.h"
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

static void test_client_create_destroy(void)
{
    // NULL API key should fail
    ClaudeClient *client = claude_client_create(NULL);
    TEST_ASSERT(client == NULL, "NULL API key returns NULL");

    // Empty API key should fail
    client = claude_client_create("");
    TEST_ASSERT(client == NULL, "Empty API key returns NULL");

    // Valid API key should succeed
    client = claude_client_create("test-api-key");
    TEST_ASSERT(client != NULL, "Valid API key creates client");
    TEST_ASSERT(client->initialized == true, "Client is initialized");
    TEST_ASSERT(claude_client_is_valid(client) == true, "Client is valid");

    claude_client_destroy(client);
    printf("  PASS: Destroy client (no crash)\n");
    inc_tests_run();
    inc_tests_passed();
}

static void test_request_init(void)
{
    ClaudeMessageRequest req;
    claude_request_init(&req);

    TEST_ASSERT(strcmp(req.model, CLAUDE_DEFAULT_MODEL) == 0, "Default model set");
    TEST_ASSERT(req.max_tokens == CLAUDE_DEFAULT_MAX_TOKENS, "Default max tokens");
    TEST_ASSERT(req.system_prompt[0] == '\0', "System prompt empty");
    TEST_ASSERT(req.messages == NULL, "Messages array NULL");
    TEST_ASSERT(req.message_count == 0, "Message count 0");

    claude_request_cleanup(&req);
}

static void test_request_model(void)
{
    ClaudeMessageRequest req;
    claude_request_init(&req);

    claude_request_set_model(&req, "claude-haiku-4-5-20251101");
    TEST_ASSERT(strcmp(req.model, "claude-haiku-4-5-20251101") == 0, "Model updated");

    claude_request_cleanup(&req);
}

static void test_request_max_tokens(void)
{
    ClaudeMessageRequest req;
    claude_request_init(&req);

    claude_request_set_max_tokens(&req, 1000);
    TEST_ASSERT(req.max_tokens == 1000, "Max tokens set to 1000");

    claude_request_set_max_tokens(&req, -100);
    TEST_ASSERT(req.max_tokens == CLAUDE_DEFAULT_MAX_TOKENS, "Invalid max tokens uses default");

    claude_request_cleanup(&req);
}

static void test_request_system_prompt(void)
{
    ClaudeMessageRequest req;
    claude_request_init(&req);

    claude_request_set_system_prompt(&req, "You are a helpful assistant.");
    TEST_ASSERT(strcmp(req.system_prompt, "You are a helpful assistant.") == 0, "System prompt set");

    claude_request_cleanup(&req);
}

static void test_request_messages(void)
{
    ClaudeMessageRequest req;
    claude_request_init(&req);

    claude_request_add_user_message(&req, "Hello!");
    TEST_ASSERT(req.message_count == 1, "Message count is 1");
    TEST_ASSERT(strcmp(req.messages[0].role, "user") == 0, "First message is user");
    TEST_ASSERT(strcmp(req.messages[0].content, "Hello!") == 0, "Message content correct");

    claude_request_add_assistant_message(&req, "Hi there!");
    TEST_ASSERT(req.message_count == 2, "Message count is 2");
    TEST_ASSERT(strcmp(req.messages[1].role, "assistant") == 0, "Second message is assistant");

    claude_request_add_user_message(&req, "How are you?");
    TEST_ASSERT(req.message_count == 3, "Message count is 3");

    claude_request_cleanup(&req);
    TEST_ASSERT(req.messages == NULL, "Messages cleaned up");
    TEST_ASSERT(req.message_count == 0, "Message count reset");
}

static void test_response_init(void)
{
    ClaudeMessageResponse resp;
    claude_response_init(&resp);

    TEST_ASSERT(resp.id[0] == '\0', "ID empty");
    TEST_ASSERT(resp.content[0] == '\0', "Content empty");
    TEST_ASSERT(resp.stop_reason == CLAUDE_STOP_END_TURN, "Default stop reason");
    TEST_ASSERT(resp.tool_uses == NULL, "Tool uses NULL");
    TEST_ASSERT(resp.error == NULL, "Error NULL");
}

static void test_stop_reason_strings(void)
{
    TEST_ASSERT(strcmp(claude_stop_reason_to_string(CLAUDE_STOP_END_TURN), "end_turn") == 0, "end_turn string");
    TEST_ASSERT(strcmp(claude_stop_reason_to_string(CLAUDE_STOP_MAX_TOKENS), "max_tokens") == 0, "max_tokens string");
    TEST_ASSERT(strcmp(claude_stop_reason_to_string(CLAUDE_STOP_TOOL_USE), "tool_use") == 0, "tool_use string");
    TEST_ASSERT(strcmp(claude_stop_reason_to_string(CLAUDE_STOP_ERROR), "error") == 0, "error string");

    TEST_ASSERT(claude_stop_reason_from_string("end_turn") == CLAUDE_STOP_END_TURN, "Parse end_turn");
    TEST_ASSERT(claude_stop_reason_from_string("max_tokens") == CLAUDE_STOP_MAX_TOKENS, "Parse max_tokens");
    TEST_ASSERT(claude_stop_reason_from_string("tool_use") == CLAUDE_STOP_TOOL_USE, "Parse tool_use");
    TEST_ASSERT(claude_stop_reason_from_string("invalid") == CLAUDE_STOP_ERROR, "Invalid parses as error");
}

static void test_parse_response_success(void)
{
    const char *json =
        "{"
        "  \"id\": \"msg_123\","
        "  \"type\": \"message\","
        "  \"role\": \"assistant\","
        "  \"content\": ["
        "    {\"type\": \"text\", \"text\": \"Hello! How can I help?\"}"
        "  ],"
        "  \"stop_reason\": \"end_turn\","
        "  \"usage\": {"
        "    \"input_tokens\": 10,"
        "    \"output_tokens\": 20"
        "  }"
        "}";

    ClaudeMessageResponse resp;
    bool success = claude_parse_response(json, &resp);

    TEST_ASSERT(success == true, "Parse succeeds");
    TEST_ASSERT(strcmp(resp.id, "msg_123") == 0, "ID parsed");
    TEST_ASSERT(strstr(resp.content, "Hello! How can I help?") != NULL, "Content parsed");
    TEST_ASSERT(resp.stop_reason == CLAUDE_STOP_END_TURN, "Stop reason parsed");
    TEST_ASSERT(resp.input_tokens == 10, "Input tokens parsed");
    TEST_ASSERT(resp.output_tokens == 20, "Output tokens parsed");

    claude_response_cleanup(&resp);
}

static void test_parse_response_tool_use(void)
{
    const char *json =
        "{"
        "  \"id\": \"msg_456\","
        "  \"content\": ["
        "    {\"type\": \"text\", \"text\": \"I'll help with that.\"},"
        "    {"
        "      \"type\": \"tool_use\","
        "      \"id\": \"tool_1\","
        "      \"name\": \"file_list\","
        "      \"input\": {\"path\": \"/Users\"}"
        "    }"
        "  ],"
        "  \"stop_reason\": \"tool_use\","
        "  \"usage\": {\"input_tokens\": 5, \"output_tokens\": 15}"
        "}";

    ClaudeMessageResponse resp;
    bool success = claude_parse_response(json, &resp);

    TEST_ASSERT(success == true, "Parse succeeds");
    TEST_ASSERT(resp.stop_reason == CLAUDE_STOP_TOOL_USE, "Stop reason is tool_use");
    TEST_ASSERT(resp.tool_use_count == 1, "One tool use parsed");
    TEST_ASSERT(claude_response_has_tool_use(&resp) == true, "Has tool use");

    if (resp.tool_uses) {
        TEST_ASSERT(strcmp(resp.tool_uses[0].id, "tool_1") == 0, "Tool ID correct");
        TEST_ASSERT(strcmp(resp.tool_uses[0].name, "file_list") == 0, "Tool name correct");
        TEST_ASSERT(strstr(resp.tool_uses[0].input, "/Users") != NULL, "Tool input contains path");
    }

    claude_response_cleanup(&resp);
}

static void test_parse_response_error(void)
{
    const char *json =
        "{"
        "  \"error\": {"
        "    \"type\": \"invalid_request_error\","
        "    \"message\": \"Invalid API key\""
        "  }"
        "}";

    ClaudeMessageResponse resp;
    bool success = claude_parse_response(json, &resp);

    TEST_ASSERT(success == false, "Parse returns false for error");
    TEST_ASSERT(resp.stop_reason == CLAUDE_STOP_ERROR, "Stop reason is error");
    TEST_ASSERT(resp.error != NULL, "Error message set");
    TEST_ASSERT(strstr(resp.error, "Invalid API key") != NULL, "Error message correct");

    claude_response_cleanup(&resp);
}

static void test_auth_from_env(void)
{
    AuthState auth;
    auth_init(&auth);

    TEST_ASSERT(auth.status == AUTH_STATUS_UNKNOWN, "Initial status unknown");
    TEST_ASSERT(auth.source == AUTH_SOURCE_NONE, "Initial source none");

    // Try loading (may or may not succeed depending on environment)
    bool loaded = auth_load_from_env(&auth);
    if (loaded) {
        TEST_ASSERT(auth.source == AUTH_SOURCE_ENV, "Source is ENV");
        TEST_ASSERT(strlen(auth.api_key) > 0, "API key loaded");
        printf("  INFO: API key found in environment\n");
    } else {
        printf("  INFO: No API key in environment (expected if CLAUDE_API_KEY not set)\n");
    }

    auth_clear(&auth);
    TEST_ASSERT(auth.api_key[0] == '\0', "API key cleared");
}

static void test_real_api_request(void)
{
    // Only run if API key is available
    AuthState auth;
    auth_init(&auth);

    if (!auth_load_from_env(&auth)) {
        printf("  SKIP: No API key in environment\n");
        inc_tests_run();
        return;
    }

    ClaudeClient *client = claude_client_create(auth.api_key);
    if (!client) {
        printf("  SKIP: Could not create Claude client\n");
        inc_tests_run();
        auth_clear(&auth);
        return;
    }

    ClaudeMessageRequest req;
    claude_request_init(&req);
    claude_request_set_max_tokens(&req, 50);
    claude_request_add_user_message(&req, "Say 'test' and nothing else.");

    ClaudeMessageResponse resp;
    claude_response_init(&resp);

    bool success = claude_send_message(client, &req, &resp);

    if (success) {
        TEST_ASSERT(resp.stop_reason != CLAUDE_STOP_ERROR, "Request succeeded");
        TEST_ASSERT(strlen(resp.content) > 0, "Response has content");
        printf("  INFO: Claude response: %.50s...\n", resp.content);
    } else {
        printf("  SKIP: API request failed: %s\n", resp.error ? resp.error : "unknown");
        inc_tests_run();
    }

    claude_response_cleanup(&resp);
    claude_request_cleanup(&req);
    claude_client_destroy(client);
    auth_clear(&auth);
}

void test_claude_client(void)
{
    test_client_create_destroy();
    test_request_init();
    test_request_model();
    test_request_max_tokens();
    test_request_system_prompt();
    test_request_messages();
    test_response_init();
    test_stop_reason_strings();
    test_parse_response_success();
    test_parse_response_tool_use();
    test_parse_response_error();
    test_auth_from_env();
    test_real_api_request();
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "api/gemini_client.h"
#include "api/auth.h"

// Test framework functions from test_main.c
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

#define TEST_ASSERT_STR_EQ(a, b, message) do { \
    inc_tests_run(); \
    if (a && b && strcmp(a, b) == 0) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s - expected '%s', got '%s' (line %d)\n", message, b, a, __LINE__); \
    } \
} while(0)

static void test_gemini_client_create_destroy(void)
{
    printf("\n  Testing client create/destroy...\n");

    // NULL API key should fail
    GeminiClient *client = gemini_client_create(NULL);
    TEST_ASSERT(client == NULL, "NULL API key returns NULL");

    // Empty API key should fail
    client = gemini_client_create("");
    TEST_ASSERT(client == NULL, "Empty API key returns NULL");

    // Valid API key should succeed
    client = gemini_client_create("test-gemini-key-12345");
    TEST_ASSERT(client != NULL, "Valid API key creates client");
    if (client) {
        TEST_ASSERT(client->initialized == true, "Client is initialized");
        TEST_ASSERT(gemini_client_is_valid(client) == true, "Client is valid");
        gemini_client_destroy(client);
    }
}

static void test_gemini_request_init(void)
{
    printf("\n  Testing request init...\n");

    GeminiImageRequest req;
    gemini_request_init(&req);

    TEST_ASSERT(req.prompt[0] == '\0', "Prompt initialized empty");
    TEST_ASSERT_STR_EQ(req.model, GEMINI_DEFAULT_MODEL, "Default model set");
    TEST_ASSERT(req.width == 0, "Width initialized to 0");
    TEST_ASSERT(req.height == 0, "Height initialized to 0");
}

static void test_gemini_request_set_prompt(void)
{
    printf("\n  Testing request set prompt...\n");

    GeminiImageRequest req;
    gemini_request_init(&req);

    gemini_request_set_prompt(&req, "A sunset over mountains");
    TEST_ASSERT_STR_EQ(req.prompt, "A sunset over mountains", "Prompt set correctly");

    // Test long prompt truncation
    char long_prompt[GEMINI_MAX_PROMPT_LEN + 100];
    memset(long_prompt, 'a', sizeof(long_prompt) - 1);
    long_prompt[sizeof(long_prompt) - 1] = '\0';

    gemini_request_set_prompt(&req, long_prompt);
    TEST_ASSERT(strlen(req.prompt) < GEMINI_MAX_PROMPT_LEN, "Long prompt truncated");
}

static void test_gemini_request_set_model(void)
{
    printf("\n  Testing request set model...\n");

    GeminiImageRequest req;
    gemini_request_init(&req);

    gemini_request_set_model(&req, "custom-model");
    TEST_ASSERT_STR_EQ(req.model, "custom-model", "Custom model set");

    gemini_request_set_model(&req, GEMINI_QUALITY_MODEL);
    TEST_ASSERT_STR_EQ(req.model, GEMINI_QUALITY_MODEL, "Quality model set");
}

static void test_gemini_response_init_cleanup(void)
{
    printf("\n  Testing response init/cleanup...\n");

    GeminiImageResponse resp;
    gemini_response_init(&resp);

    TEST_ASSERT(resp.image_data == NULL, "Image data initialized NULL");
    TEST_ASSERT(resp.image_size == 0, "Image size initialized 0");
    TEST_ASSERT(resp.error[0] == '\0', "Error initialized empty");
    TEST_ASSERT(resp.result_type == GEMINI_RESULT_SUCCESS, "Result type initialized SUCCESS");

    // Simulate setting data
    resp.image_data = malloc(100);
    resp.image_size = 100;

    gemini_response_cleanup(&resp);
    TEST_ASSERT(resp.image_data == NULL, "Image data freed");
    TEST_ASSERT(resp.image_size == 0, "Image size reset");
}

static void test_gemini_format_helpers(void)
{
    printf("\n  Testing format helpers...\n");

    // Test extension from format
    TEST_ASSERT_STR_EQ(gemini_format_to_extension(GEMINI_FORMAT_PNG), ".png", "PNG extension");
    TEST_ASSERT_STR_EQ(gemini_format_to_extension(GEMINI_FORMAT_JPEG), ".jpg", "JPEG extension");
    TEST_ASSERT_STR_EQ(gemini_format_to_extension(GEMINI_FORMAT_WEBP), ".webp", "WebP extension");
    TEST_ASSERT_STR_EQ(gemini_format_to_extension(GEMINI_FORMAT_UNKNOWN), ".png", "Unknown defaults to .png");

    // Test format from mime type
    TEST_ASSERT(gemini_format_from_mime("image/png") == GEMINI_FORMAT_PNG, "Parse PNG mime");
    TEST_ASSERT(gemini_format_from_mime("image/jpeg") == GEMINI_FORMAT_JPEG, "Parse JPEG mime");
    TEST_ASSERT(gemini_format_from_mime("image/jpg") == GEMINI_FORMAT_JPEG, "Parse JPG mime");
    TEST_ASSERT(gemini_format_from_mime("image/webp") == GEMINI_FORMAT_WEBP, "Parse WebP mime");
    TEST_ASSERT(gemini_format_from_mime("application/octet-stream") == GEMINI_FORMAT_UNKNOWN, "Unknown mime type");
    TEST_ASSERT(gemini_format_from_mime(NULL) == GEMINI_FORMAT_UNKNOWN, "NULL mime type");
}

static void test_gemini_result_strings(void)
{
    printf("\n  Testing result strings...\n");

    const char *success = gemini_result_to_string(GEMINI_RESULT_SUCCESS);
    TEST_ASSERT(success != NULL && strstr(success, "uccess") != NULL, "Success string contains 'uccess'");

    const char *invalid_key = gemini_result_to_string(GEMINI_RESULT_INVALID_KEY);
    TEST_ASSERT(invalid_key != NULL && (strstr(invalid_key, "key") != NULL || strstr(invalid_key, "Key") != NULL), "Invalid key string contains 'key'");

    const char *rate_limit = gemini_result_to_string(GEMINI_RESULT_RATE_LIMIT);
    TEST_ASSERT(rate_limit != NULL && (strstr(rate_limit, "limit") != NULL || strstr(rate_limit, "Limit") != NULL), "Rate limit string contains 'limit'");

    const char *filtered = gemini_result_to_string(GEMINI_RESULT_CONTENT_FILTERED);
    TEST_ASSERT(filtered != NULL && (strstr(filtered, "filter") != NULL || strstr(filtered, "Filter") != NULL), "Content filtered string contains 'filter'");
}

static void test_gemini_auth_from_env(void)
{
    printf("\n  Testing auth from environment...\n");

    AuthState auth;
    auth_init(&auth);

    // Test loading Gemini key from env
    bool loaded = auth_load_gemini_from_env(&auth);
    if (loaded) {
        TEST_ASSERT(auth.gemini_source == AUTH_SOURCE_ENV, "Gemini source is ENV");
        TEST_ASSERT(strlen(auth.gemini_api_key) > 0, "Gemini API key loaded");
        TEST_ASSERT(auth_gemini_is_ready(&auth) == true, "Gemini auth is ready");
        printf("  INFO: GEMINI_API_KEY found in environment\n");
    } else {
        printf("  INFO: No GEMINI_API_KEY in environment (expected if not set)\n");
        TEST_ASSERT(auth.gemini_status == AUTH_STATUS_UNKNOWN, "Status unknown when not loaded");
    }

    auth_clear(&auth);
    TEST_ASSERT(auth.gemini_api_key[0] == '\0', "Gemini key cleared");
}

static void test_gemini_client_model_change(void)
{
    printf("\n  Testing client model change...\n");

    GeminiClient *client = gemini_client_create("test-key");
    TEST_ASSERT(client != NULL, "Client created");

    if (client) {
        // Check default model
        TEST_ASSERT_STR_EQ(client->current_model, GEMINI_DEFAULT_MODEL, "Default model is set");

        // Change model
        gemini_client_set_model(client, "custom-model-v2");
        TEST_ASSERT_STR_EQ(client->current_model, "custom-model-v2", "Model changed");

        gemini_client_destroy(client);
    }
}

// ============================================================================
// Image Edit Tests
// ============================================================================

static void test_gemini_edit_request_init(void)
{
    printf("\n  Testing edit request init...\n");

    GeminiImageEditRequest req;
    gemini_edit_request_init(&req);

    TEST_ASSERT(req.prompt[0] == '\0', "Edit prompt initialized empty");
    TEST_ASSERT(req.source_image_path[0] == '\0', "Source image path initialized empty");
    TEST_ASSERT_STR_EQ(req.model, GEMINI_DEFAULT_MODEL, "Edit request default model set");
}

static void test_gemini_edit_request_set_prompt(void)
{
    printf("\n  Testing edit request set prompt...\n");

    GeminiImageEditRequest req;
    gemini_edit_request_init(&req);

    gemini_edit_request_set_prompt(&req, "Make the sky blue");
    TEST_ASSERT_STR_EQ(req.prompt, "Make the sky blue", "Edit prompt set correctly");

    // Test NULL handling
    gemini_edit_request_set_prompt(&req, NULL);
    TEST_ASSERT_STR_EQ(req.prompt, "Make the sky blue", "Edit prompt unchanged with NULL");

    gemini_edit_request_set_prompt(NULL, "test");
    TEST_ASSERT(1, "NULL request doesn't crash");
}

static void test_gemini_edit_request_set_source_image(void)
{
    printf("\n  Testing edit request set source image...\n");

    GeminiImageEditRequest req;
    gemini_edit_request_init(&req);

    gemini_edit_request_set_source_image(&req, "/path/to/image.png");
    TEST_ASSERT_STR_EQ(req.source_image_path, "/path/to/image.png", "Source image path set correctly");

    // Test NULL handling
    gemini_edit_request_set_source_image(&req, NULL);
    TEST_ASSERT_STR_EQ(req.source_image_path, "/path/to/image.png", "Source path unchanged with NULL");

    gemini_edit_request_set_source_image(NULL, "/test.png");
    TEST_ASSERT(1, "NULL request doesn't crash");
}

static void test_gemini_edit_request_set_model(void)
{
    printf("\n  Testing edit request set model...\n");

    GeminiImageEditRequest req;
    gemini_edit_request_init(&req);

    gemini_edit_request_set_model(&req, "custom-edit-model");
    TEST_ASSERT_STR_EQ(req.model, "custom-edit-model", "Edit model set correctly");

    gemini_edit_request_set_model(&req, NULL);
    TEST_ASSERT_STR_EQ(req.model, "custom-edit-model", "Edit model unchanged with NULL");
}

static void test_gemini_generate_edited_path(void)
{
    printf("\n  Testing generate edited path...\n");

    char output[256];

    // Test basic path generation
    bool result = gemini_generate_edited_path("/tmp/test.png", output, sizeof(output));
    TEST_ASSERT(result == true, "Path generated successfully");
    TEST_ASSERT(strstr(output, "_edited_") != NULL, "Output contains _edited_ suffix");
    TEST_ASSERT(strstr(output, ".png") != NULL, "Output preserves .png extension");

    // Test with jpg extension
    result = gemini_generate_edited_path("/home/user/photo.jpg", output, sizeof(output));
    TEST_ASSERT(result == true, "JPG path generated successfully");
    TEST_ASSERT(strstr(output, ".jpg") != NULL, "Output preserves .jpg extension");

    // Test NULL handling
    result = gemini_generate_edited_path(NULL, output, sizeof(output));
    TEST_ASSERT(result == false, "NULL source path returns false");

    result = gemini_generate_edited_path("/tmp/test.png", NULL, sizeof(output));
    TEST_ASSERT(result == false, "NULL output returns false");

    result = gemini_generate_edited_path("/tmp/test.png", output, 0);
    TEST_ASSERT(result == false, "Zero output size returns false");

    // Test small buffer
    char small[10];
    result = gemini_generate_edited_path("/tmp/test.png", small, sizeof(small));
    TEST_ASSERT(result == false, "Small buffer returns false");
}

static void test_gemini_edit_image_validation(void)
{
    printf("\n  Testing edit image validation...\n");

    GeminiClient *client = gemini_client_create("test-key");
    TEST_ASSERT(client != NULL, "Client created for validation test");

    if (client) {
        GeminiImageEditRequest req;
        gemini_edit_request_init(&req);

        GeminiImageResponse resp;
        gemini_response_init(&resp);

        // Test with empty prompt
        gemini_edit_request_set_source_image(&req, "/tmp/test.png");
        bool result = gemini_edit_image(client, &req, &resp);
        TEST_ASSERT(result == false, "Empty prompt returns false");
        TEST_ASSERT(resp.result_type == GEMINI_RESULT_ERROR, "Error type set for empty prompt");

        // Test with empty source image
        gemini_edit_request_init(&req);
        gemini_edit_request_set_prompt(&req, "Make it blue");
        result = gemini_edit_image(client, &req, &resp);
        TEST_ASSERT(result == false, "Empty source image returns false");

        // Test NULL parameters
        result = gemini_edit_image(NULL, &req, &resp);
        TEST_ASSERT(result == false, "NULL client returns false");

        result = gemini_edit_image(client, NULL, &resp);
        TEST_ASSERT(result == false, "NULL request returns false");

        result = gemini_edit_image(client, &req, NULL);
        TEST_ASSERT(result == false, "NULL response returns false");

        gemini_response_cleanup(&resp);
        gemini_client_destroy(client);
    }
}

static void test_real_image_generation(void)
{
    printf("\n  Testing real image generation (requires GEMINI_API_KEY)...\n");

    // Only run if API key is available
    AuthState auth;
    auth_init(&auth);

    if (!auth_load_gemini_from_env(&auth)) {
        printf("  SKIP: No GEMINI_API_KEY in environment\n");
        return;
    }

    GeminiClient *client = gemini_client_create(auth.gemini_api_key);
    if (!client) {
        printf("  SKIP: Could not create Gemini client\n");
        auth_clear(&auth);
        return;
    }

    GeminiImageRequest req;
    gemini_request_init(&req);
    gemini_request_set_prompt(&req, "A simple red circle on a white background");

    GeminiImageResponse resp;
    gemini_response_init(&resp);

    printf("  INFO: Generating test image (this may take several seconds)...\n");
    bool success = gemini_generate_image(client, &req, &resp);

    if (success && resp.result_type == GEMINI_RESULT_SUCCESS) {
        TEST_ASSERT(resp.image_data != NULL, "Image data received");
        TEST_ASSERT(resp.image_size > 0, "Image has content");
        printf("  INFO: Generated image: %zu bytes, format: %s\n",
               resp.image_size, resp.mime_type);

        // Test saving
        char test_path[256];
        snprintf(test_path, sizeof(test_path), "/tmp/gemini_test_%d%s",
                 getpid(), gemini_format_to_extension(resp.format));

        bool saved = gemini_save_image(&resp, test_path);
        TEST_ASSERT(saved == true, "Image saved to disk");

        // Verify file exists
        struct stat st;
        TEST_ASSERT(stat(test_path, &st) == 0, "Image file exists");
        TEST_ASSERT(st.st_size > 0, "Image file has content");

        printf("  INFO: Image saved to %s (%lld bytes)\n", test_path, (long long)st.st_size);

        // Clean up test file
        unlink(test_path);
    } else {
        printf("  SKIP: Image generation failed: %s\n",
               resp.error[0] ? resp.error : gemini_result_to_string(resp.result_type));
    }

    gemini_response_cleanup(&resp);
    gemini_client_destroy(client);
    auth_clear(&auth);
}

void test_gemini_client(void)
{
    printf("\n[Gemini Client Tests]\n");

    test_gemini_client_create_destroy();
    test_gemini_request_init();
    test_gemini_request_set_prompt();
    test_gemini_request_set_model();
    test_gemini_response_init_cleanup();
    test_gemini_format_helpers();
    test_gemini_result_strings();
    test_gemini_auth_from_env();
    test_gemini_client_model_change();
    test_gemini_edit_request_init();
    test_gemini_edit_request_set_prompt();
    test_gemini_edit_request_set_source_image();
    test_gemini_edit_request_set_model();
    test_gemini_generate_edited_path();
    test_gemini_edit_image_validation();
    test_real_image_generation();
}

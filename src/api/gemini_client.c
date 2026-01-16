#include "gemini_client.h"
#include "http_client.h"
#include "../../external/cJSON/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if GEMINI_DEBUG
#define GEMINI_LOG(fmt, ...) fprintf(stderr, "[GEMINI] " fmt "\n", ##__VA_ARGS__)
#else
#define GEMINI_LOG(fmt, ...) ((void)0)
#endif

// Base64 decoding table
static const unsigned char base64_decode_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

static size_t base64_decode(const char *input, unsigned char **output)
{
    if (!input || !output) return 0;

    size_t input_len = strlen(input);
    if (input_len == 0) return 0;

    // Calculate output size (approximately 3/4 of input)
    size_t output_len = (input_len * 3) / 4;

    // Account for padding
    if (input_len >= 1 && input[input_len - 1] == '=') output_len--;
    if (input_len >= 2 && input[input_len - 2] == '=') output_len--;

    *output = (unsigned char *)malloc(output_len + 1);
    if (!*output) return 0;

    size_t i = 0, j = 0;
    unsigned char sextet[4];
    int count = 0;

    while (i < input_len) {
        unsigned char c = (unsigned char)input[i++];

        // Skip whitespace
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;

        // Stop at padding
        if (c == '=') break;

        unsigned char val = base64_decode_table[c];
        if (val == 64) continue; // Invalid character, skip

        sextet[count++] = val;

        if (count == 4) {
            if (j + 3 <= output_len) {
                (*output)[j++] = (sextet[0] << 2) | (sextet[1] >> 4);
                (*output)[j++] = (sextet[1] << 4) | (sextet[2] >> 2);
                (*output)[j++] = (sextet[2] << 6) | sextet[3];
            }
            count = 0;
        }
    }

    // Handle remaining bytes
    if (count >= 2 && j < output_len) {
        (*output)[j++] = (sextet[0] << 2) | (sextet[1] >> 4);
    }
    if (count >= 3 && j < output_len) {
        (*output)[j++] = (sextet[1] << 4) | (sextet[2] >> 2);
    }

    return j;
}

// Base64 encoding table
static const char base64_encode_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const unsigned char *input, size_t input_len)
{
    if (!input || input_len == 0) return NULL;

    // Calculate output size (4 bytes for every 3 input bytes, plus padding)
    size_t output_len = 4 * ((input_len + 2) / 3);
    char *output = (char *)malloc(output_len + 1);
    if (!output) return NULL;

    size_t i = 0, j = 0;
    while (i < input_len) {
        uint32_t octet_a = i < input_len ? input[i++] : 0;
        uint32_t octet_b = i < input_len ? input[i++] : 0;
        uint32_t octet_c = i < input_len ? input[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output[j++] = base64_encode_table[(triple >> 18) & 0x3F];
        output[j++] = base64_encode_table[(triple >> 12) & 0x3F];
        output[j++] = base64_encode_table[(triple >> 6) & 0x3F];
        output[j++] = base64_encode_table[triple & 0x3F];
    }

    // Add padding
    int padding = (3 - (int)(input_len % 3)) % 3;
    for (int p = 0; p < padding; p++) {
        output[output_len - 1 - p] = '=';
    }

    output[output_len] = '\0';
    return output;
}

// Read file into memory
static unsigned char *read_file_binary(const char *path, size_t *size_out)
{
    if (!path || !size_out) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)file_size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(data, 1, (size_t)file_size, f);
    fclose(f);

    if (read_size != (size_t)file_size) {
        free(data);
        return NULL;
    }

    *size_out = (size_t)file_size;
    return data;
}

// Get MIME type from file extension
static const char *get_mime_from_extension(const char *path)
{
    if (!path) return "application/octet-stream";

    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    ext++; // Skip the dot
    if (strcasecmp(ext, "png") == 0) return "image/png";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, "gif") == 0) return "image/gif";
    if (strcasecmp(ext, "webp") == 0) return "image/webp";
    if (strcasecmp(ext, "bmp") == 0) return "image/bmp";

    return "application/octet-stream";
}

// Forward declaration for response parsing
static bool gemini_parse_image_response(const char *json, GeminiImageResponse *resp);

GeminiClient *gemini_client_create(const char *api_key)
{
    if (!api_key || strlen(api_key) == 0) return NULL;

    GeminiClient *client = (GeminiClient *)calloc(1, sizeof(GeminiClient));
    if (!client) return NULL;

    strncpy(client->api_key, api_key, GEMINI_MAX_API_KEY_LEN - 1);
    client->api_key[GEMINI_MAX_API_KEY_LEN - 1] = '\0';
    strncpy(client->current_model, GEMINI_DEFAULT_MODEL, GEMINI_MAX_MODEL_LEN - 1);
    client->current_model[GEMINI_MAX_MODEL_LEN - 1] = '\0';
    client->initialized = true;

    return client;
}

void gemini_client_destroy(GeminiClient *client)
{
    if (client) {
        memset(client->api_key, 0, GEMINI_MAX_API_KEY_LEN);
        free(client);
    }
}

bool gemini_client_is_valid(const GeminiClient *client)
{
    return client && client->initialized && strlen(client->api_key) > 0;
}

void gemini_client_set_model(GeminiClient *client, const char *model)
{
    if (!client || !model) return;
    strncpy(client->current_model, model, GEMINI_MAX_MODEL_LEN - 1);
    client->current_model[GEMINI_MAX_MODEL_LEN - 1] = '\0';
}

void gemini_request_init(GeminiImageRequest *req)
{
    if (!req) return;
    memset(req, 0, sizeof(GeminiImageRequest));
    strncpy(req->model, GEMINI_DEFAULT_MODEL, GEMINI_MAX_MODEL_LEN - 1);
}

void gemini_request_set_prompt(GeminiImageRequest *req, const char *prompt)
{
    if (!req || !prompt) return;
    strncpy(req->prompt, prompt, GEMINI_MAX_PROMPT_LEN - 1);
    req->prompt[GEMINI_MAX_PROMPT_LEN - 1] = '\0';
}

void gemini_request_set_model(GeminiImageRequest *req, const char *model)
{
    if (!req || !model) return;
    strncpy(req->model, model, GEMINI_MAX_MODEL_LEN - 1);
    req->model[GEMINI_MAX_MODEL_LEN - 1] = '\0';
}

void gemini_response_init(GeminiImageResponse *resp)
{
    if (!resp) return;
    memset(resp, 0, sizeof(GeminiImageResponse));
    resp->result_type = GEMINI_RESULT_SUCCESS;
}

void gemini_response_cleanup(GeminiImageResponse *resp)
{
    if (!resp) return;
    if (resp->image_data) {
        free(resp->image_data);
        resp->image_data = NULL;
    }
    resp->image_size = 0;
}

bool gemini_generate_image(GeminiClient *client,
                           const GeminiImageRequest *req,
                           GeminiImageResponse *resp)
{
    GEMINI_LOG("=== gemini_generate_image START ===");

    if (!client || !req || !resp) {
        GEMINI_LOG("ERROR: NULL parameter (client=%p, req=%p, resp=%p)",
                   (void*)client, (void*)req, (void*)resp);
        return false;
    }

    gemini_response_init(resp);

    if (!gemini_client_is_valid(client)) {
        GEMINI_LOG("ERROR: Invalid Gemini client");
        strncpy(resp->error, "Invalid Gemini client", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        return false;
    }

    GEMINI_LOG("Client valid, API key length: %zu", strlen(client->api_key));
    GEMINI_LOG("Prompt: %.100s%s", req->prompt, strlen(req->prompt) > 100 ? "..." : "");

    if (strlen(req->prompt) == 0) {
        GEMINI_LOG("ERROR: Empty prompt");
        strncpy(resp->error, "Empty prompt", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        return false;
    }

    // Build URL
    char url[512];
    const char *model = req->model[0] ? req->model : client->current_model;
    snprintf(url, sizeof(url), "%s/%s:generateContent", GEMINI_API_BASE_URL, model);
    GEMINI_LOG("URL: %s", url);
    GEMINI_LOG("Model: %s", model);

    // Build request body
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateArray();
    cJSON *content = cJSON_CreateObject();
    cJSON *parts = cJSON_CreateArray();
    cJSON *part = cJSON_CreateObject();

    cJSON_AddStringToObject(part, "text", req->prompt);
    cJSON_AddItemToArray(parts, part);
    cJSON_AddItemToObject(content, "parts", parts);
    cJSON_AddItemToArray(contents, content);
    cJSON_AddItemToObject(root, "contents", contents);

    // Add generationConfig for image output (TEXT required with IMAGE per API docs)
    cJSON *gen_config = cJSON_CreateObject();
    cJSON *modalities = cJSON_CreateArray();
    cJSON_AddItemToArray(modalities, cJSON_CreateString("TEXT"));
    cJSON_AddItemToArray(modalities, cJSON_CreateString("IMAGE"));
    cJSON_AddItemToObject(gen_config, "responseModalities", modalities);
    cJSON_AddItemToObject(root, "generationConfig", gen_config);

    char *request_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!request_body) {
        GEMINI_LOG("ERROR: Failed to build request JSON");
        strncpy(resp->error, "Failed to build request JSON", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        return false;
    }

    GEMINI_LOG("Request body: %.500s%s", request_body, strlen(request_body) > 500 ? "..." : "");

    // Make HTTP request
    HttpClient *http_client = http_client_create();
    if (!http_client) {
        GEMINI_LOG("ERROR: Failed to create HTTP client");
        strncpy(resp->error, "Failed to create HTTP client", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        free(request_body);
        return false;
    }

    // Set longer timeout for image generation (60 seconds)
    http_client_set_timeout(http_client, 30, 60);

    HttpRequest http_req;
    http_request_init(&http_req);
    http_request_set_method(&http_req, HTTP_POST);
    http_request_set_url(&http_req, url);
    http_request_add_header(&http_req, "Content-Type", "application/json");
    http_request_add_header(&http_req, "x-goog-api-key", client->api_key);
    http_request_set_body_string(&http_req, request_body);
    free(request_body);

    HttpResponse http_resp;
    http_response_init(&http_resp);

    GEMINI_LOG("Sending HTTP request...");
    bool success = http_client_execute(http_client, &http_req, &http_resp);
    http_request_cleanup(&http_req);
    http_client_destroy(http_client);

    GEMINI_LOG("HTTP request complete, success=%d", success);

    if (!success) {
        GEMINI_LOG("ERROR: HTTP request failed: %s", http_resp.error ? http_resp.error : "unknown");
        snprintf(resp->error, sizeof(resp->error), "HTTP request failed: %s",
                 http_resp.error ? http_resp.error : "unknown");
        resp->result_type = GEMINI_RESULT_ERROR;
        http_response_cleanup(&http_resp);
        return false;
    }

    GEMINI_LOG("HTTP status code: %d", http_resp.status_code);
    if (http_resp.body) {
        GEMINI_LOG("Response body length: %zu", strlen(http_resp.body));
        GEMINI_LOG("Response body (first 1000 chars): %.1000s", http_resp.body);
    } else {
        GEMINI_LOG("Response body: NULL");
    }

    // Check HTTP status
    if (http_resp.status_code == 401 || http_resp.status_code == 403) {
        GEMINI_LOG("ERROR: Invalid API key (HTTP %d)", http_resp.status_code);
        strncpy(resp->error, "Invalid API key", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_INVALID_KEY;
        http_response_cleanup(&http_resp);
        return false;
    }

    if (http_resp.status_code == 429) {
        GEMINI_LOG("ERROR: Rate limit exceeded");
        strncpy(resp->error, "Rate limit exceeded", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_RATE_LIMIT;
        http_response_cleanup(&http_resp);
        return false;
    }

    if (http_resp.status_code != 200) {
        GEMINI_LOG("ERROR: HTTP error %d", http_resp.status_code);
        snprintf(resp->error, sizeof(resp->error), "HTTP error: %d", http_resp.status_code);
        resp->result_type = GEMINI_RESULT_ERROR;
        http_response_cleanup(&http_resp);
        return false;
    }

    // Parse response
    GEMINI_LOG("Parsing response...");
    success = gemini_parse_image_response(http_resp.body, resp);
    GEMINI_LOG("Parse result: success=%d, result_type=%d", success, resp->result_type);
    if (!success && resp->error[0]) {
        GEMINI_LOG("Parse error: %s", resp->error);
    }
    http_response_cleanup(&http_resp);

    GEMINI_LOG("=== gemini_generate_image END (success=%d) ===", success);
    return success;
}

static bool gemini_parse_image_response(const char *json, GeminiImageResponse *resp)
{
    GEMINI_LOG("=== gemini_parse_image_response START ===");

    if (!json || !resp) {
        GEMINI_LOG("ERROR: NULL parameter");
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        GEMINI_LOG("ERROR: Failed to parse JSON");
        strncpy(resp->error, "Failed to parse response JSON", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        return false;
    }

    // Check for API error
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *message = cJSON_GetObjectItem(error, "message");
        if (message && cJSON_IsString(message)) {
            GEMINI_LOG("API ERROR: %s", message->valuestring);
            strncpy(resp->error, message->valuestring, GEMINI_MAX_ERROR_LEN - 1);
        }
        cJSON *code = cJSON_GetObjectItem(error, "code");
        if (code && cJSON_IsNumber(code)) {
            GEMINI_LOG("API ERROR code: %d", code->valueint);
            if (code->valueint == 401 || code->valueint == 403) {
                resp->result_type = GEMINI_RESULT_INVALID_KEY;
            } else if (code->valueint == 429) {
                resp->result_type = GEMINI_RESULT_RATE_LIMIT;
            } else {
                resp->result_type = GEMINI_RESULT_ERROR;
            }
        } else {
            resp->result_type = GEMINI_RESULT_ERROR;
        }
        cJSON_Delete(root);
        return false;
    }

    // Navigate: candidates[0].content.parts[*].inlineData
    cJSON *candidates = cJSON_GetObjectItem(root, "candidates");
    if (!candidates || !cJSON_IsArray(candidates) || cJSON_GetArraySize(candidates) == 0) {
        GEMINI_LOG("ERROR: No candidates in response");
        strncpy(resp->error, "No candidates in response", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        cJSON_Delete(root);
        return false;
    }

    GEMINI_LOG("Found %d candidates", cJSON_GetArraySize(candidates));
    cJSON *candidate = cJSON_GetArrayItem(candidates, 0);

    // Check for content filtering
    cJSON *finish_reason = cJSON_GetObjectItem(candidate, "finishReason");
    if (finish_reason && cJSON_IsString(finish_reason)) {
        GEMINI_LOG("Finish reason: %s", finish_reason->valuestring);
        if (strcmp(finish_reason->valuestring, "SAFETY") == 0 ||
            strcmp(finish_reason->valuestring, "BLOCKED") == 0) {
            GEMINI_LOG("ERROR: Content blocked by safety filters");
            strncpy(resp->error, "Content was blocked by safety filters", GEMINI_MAX_ERROR_LEN - 1);
            resp->result_type = GEMINI_RESULT_CONTENT_FILTERED;
            cJSON_Delete(root);
            return false;
        }
    }

    cJSON *content = cJSON_GetObjectItem(candidate, "content");
    cJSON *parts = content ? cJSON_GetObjectItem(content, "parts") : NULL;

    if (!parts || !cJSON_IsArray(parts)) {
        GEMINI_LOG("ERROR: No parts in response (content=%p, parts=%p)", (void*)content, (void*)parts);
        strncpy(resp->error, "No parts in response", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        cJSON_Delete(root);
        return false;
    }

    // Find inlineData in parts
    int parts_count = cJSON_GetArraySize(parts);
    GEMINI_LOG("Found %d parts in response", parts_count);

    for (int i = 0; i < parts_count; i++) {
        cJSON *p = cJSON_GetArrayItem(parts, i);
        cJSON *inline_data = cJSON_GetObjectItem(p, "inlineData");

        // Log what's in this part
        cJSON *text_part = cJSON_GetObjectItem(p, "text");
        if (text_part && cJSON_IsString(text_part)) {
            GEMINI_LOG("Part %d: text=%.100s%s", i, text_part->valuestring,
                       strlen(text_part->valuestring) > 100 ? "..." : "");
        }

        if (inline_data) {
            GEMINI_LOG("Part %d: found inlineData", i);
            cJSON *mime = cJSON_GetObjectItem(inline_data, "mimeType");
            cJSON *data = cJSON_GetObjectItem(inline_data, "data");

            if (mime && cJSON_IsString(mime) && data && cJSON_IsString(data)) {
                GEMINI_LOG("Part %d: mimeType=%s, data length=%zu", i,
                           mime->valuestring, strlen(data->valuestring));

                strncpy(resp->mime_type, mime->valuestring, sizeof(resp->mime_type) - 1);
                resp->format = gemini_format_from_mime(mime->valuestring);

                // Decode base64
                resp->image_size = base64_decode(data->valuestring, &resp->image_data);
                GEMINI_LOG("Decoded image size: %zu bytes", resp->image_size);

                if (resp->image_size > 0 && resp->image_data) {
                    resp->result_type = GEMINI_RESULT_SUCCESS;
                    GEMINI_LOG("SUCCESS: Image decoded successfully");
                    cJSON_Delete(root);
                    return true;
                } else {
                    GEMINI_LOG("ERROR: Base64 decode failed or empty");
                }
            } else {
                GEMINI_LOG("Part %d: inlineData missing mime or data", i);
            }
        }
    }

    GEMINI_LOG("ERROR: No image data found in any part");
    strncpy(resp->error, "No image data in response", GEMINI_MAX_ERROR_LEN - 1);
    resp->result_type = GEMINI_RESULT_ERROR;
    cJSON_Delete(root);
    return false;
}

bool gemini_save_image(const GeminiImageResponse *resp, const char *path)
{
    if (!resp || !resp->image_data || resp->image_size == 0 || !path) {
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    size_t written = fwrite(resp->image_data, 1, resp->image_size, f);
    fclose(f);

    return written == resp->image_size;
}

const char *gemini_result_to_string(GeminiResultType result)
{
    switch (result) {
        case GEMINI_RESULT_SUCCESS: return "Success";
        case GEMINI_RESULT_ERROR: return "Error";
        case GEMINI_RESULT_INVALID_KEY: return "Invalid API key";
        case GEMINI_RESULT_RATE_LIMIT: return "Rate limit exceeded";
        case GEMINI_RESULT_CONTENT_FILTERED: return "Content filtered";
        default: return "Unknown";
    }
}

const char *gemini_format_to_extension(GeminiImageFormat format)
{
    switch (format) {
        case GEMINI_FORMAT_PNG: return ".png";
        case GEMINI_FORMAT_JPEG: return ".jpg";
        case GEMINI_FORMAT_WEBP: return ".webp";
        default: return ".png";
    }
}

GeminiImageFormat gemini_format_from_mime(const char *mime_type)
{
    if (!mime_type) return GEMINI_FORMAT_UNKNOWN;
    if (strstr(mime_type, "png")) return GEMINI_FORMAT_PNG;
    if (strstr(mime_type, "jpeg") || strstr(mime_type, "jpg")) return GEMINI_FORMAT_JPEG;
    if (strstr(mime_type, "webp")) return GEMINI_FORMAT_WEBP;
    return GEMINI_FORMAT_UNKNOWN;
}

// ============================================================================
// Image Editing Functions
// ============================================================================

void gemini_edit_request_init(GeminiImageEditRequest *req)
{
    if (!req) return;
    memset(req, 0, sizeof(GeminiImageEditRequest));
    strncpy(req->model, GEMINI_DEFAULT_MODEL, GEMINI_MAX_MODEL_LEN - 1);
}

void gemini_edit_request_set_prompt(GeminiImageEditRequest *req, const char *prompt)
{
    if (!req || !prompt) return;
    strncpy(req->prompt, prompt, GEMINI_MAX_PROMPT_LEN - 1);
    req->prompt[GEMINI_MAX_PROMPT_LEN - 1] = '\0';
}

void gemini_edit_request_set_source_image(GeminiImageEditRequest *req, const char *path)
{
    if (!req || !path) return;
    strncpy(req->source_image_path, path, GEMINI_MAX_PATH_LEN - 1);
    req->source_image_path[GEMINI_MAX_PATH_LEN - 1] = '\0';
}

void gemini_edit_request_set_model(GeminiImageEditRequest *req, const char *model)
{
    if (!req || !model) return;
    strncpy(req->model, model, GEMINI_MAX_MODEL_LEN - 1);
    req->model[GEMINI_MAX_MODEL_LEN - 1] = '\0';
}

bool gemini_edit_image(GeminiClient *client,
                       const GeminiImageEditRequest *req,
                       GeminiImageResponse *resp)
{
    GEMINI_LOG("=== gemini_edit_image START ===");

    if (!client || !req || !resp) {
        GEMINI_LOG("ERROR: NULL parameter");
        return false;
    }

    gemini_response_init(resp);

    if (!gemini_client_is_valid(client)) {
        GEMINI_LOG("ERROR: Invalid Gemini client");
        strncpy(resp->error, "Invalid Gemini client", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        return false;
    }

    if (strlen(req->prompt) == 0) {
        GEMINI_LOG("ERROR: Empty prompt");
        strncpy(resp->error, "Empty edit prompt", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        return false;
    }

    if (strlen(req->source_image_path) == 0) {
        GEMINI_LOG("ERROR: No source image path");
        strncpy(resp->error, "No source image specified", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        return false;
    }

    // Read source image
    size_t image_size = 0;
    unsigned char *image_data = read_file_binary(req->source_image_path, &image_size);
    if (!image_data) {
        GEMINI_LOG("ERROR: Could not read source image: %s", req->source_image_path);
        strncpy(resp->error, "Could not read source image", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        return false;
    }

    GEMINI_LOG("Read source image: %zu bytes", image_size);

    // Base64 encode the image
    char *base64_image = base64_encode(image_data, image_size);
    free(image_data);

    if (!base64_image) {
        GEMINI_LOG("ERROR: Base64 encoding failed");
        strncpy(resp->error, "Failed to encode source image", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        return false;
    }

    GEMINI_LOG("Base64 encoded image: %zu chars", strlen(base64_image));

    // Get MIME type
    const char *mime_type = get_mime_from_extension(req->source_image_path);
    GEMINI_LOG("Source image MIME type: %s", mime_type);

    // Build URL
    char url[512];
    const char *model = req->model[0] ? req->model : client->current_model;
    snprintf(url, sizeof(url), "%s/%s:generateContent", GEMINI_API_BASE_URL, model);
    GEMINI_LOG("URL: %s", url);

    // Build request body with image and text
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateArray();
    cJSON *content = cJSON_CreateObject();
    cJSON *parts = cJSON_CreateArray();

    // Add image part first
    cJSON *image_part = cJSON_CreateObject();
    cJSON *inline_data = cJSON_CreateObject();
    cJSON_AddStringToObject(inline_data, "mimeType", mime_type);
    cJSON_AddStringToObject(inline_data, "data", base64_image);
    cJSON_AddItemToObject(image_part, "inlineData", inline_data);
    cJSON_AddItemToArray(parts, image_part);

    // Add text prompt
    cJSON *text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(text_part, "text", req->prompt);
    cJSON_AddItemToArray(parts, text_part);

    cJSON_AddItemToObject(content, "parts", parts);
    cJSON_AddItemToArray(contents, content);
    cJSON_AddItemToObject(root, "contents", contents);

    // Add generation config for image output
    cJSON *gen_config = cJSON_CreateObject();
    cJSON *modalities = cJSON_CreateArray();
    cJSON_AddItemToArray(modalities, cJSON_CreateString("TEXT"));
    cJSON_AddItemToArray(modalities, cJSON_CreateString("IMAGE"));
    cJSON_AddItemToObject(gen_config, "responseModalities", modalities);
    cJSON_AddItemToObject(root, "generationConfig", gen_config);

    char *request_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(base64_image);

    if (!request_body) {
        GEMINI_LOG("ERROR: Failed to build request JSON");
        strncpy(resp->error, "Failed to build request JSON", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        return false;
    }

    GEMINI_LOG("Request body length: %zu", strlen(request_body));

    // Make HTTP request
    HttpClient *http_client = http_client_create();
    if (!http_client) {
        GEMINI_LOG("ERROR: Failed to create HTTP client");
        strncpy(resp->error, "Failed to create HTTP client", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_ERROR;
        free(request_body);
        return false;
    }

    // Set longer timeout for image editing (90 seconds)
    http_client_set_timeout(http_client, 30, 90);

    HttpRequest http_req;
    http_request_init(&http_req);
    http_request_set_method(&http_req, HTTP_POST);
    http_request_set_url(&http_req, url);
    http_request_add_header(&http_req, "Content-Type", "application/json");
    http_request_add_header(&http_req, "x-goog-api-key", client->api_key);
    http_request_set_body_string(&http_req, request_body);
    free(request_body);

    HttpResponse http_resp;
    http_response_init(&http_resp);

    GEMINI_LOG("Sending HTTP request...");
    bool success = http_client_execute(http_client, &http_req, &http_resp);
    http_request_cleanup(&http_req);
    http_client_destroy(http_client);

    GEMINI_LOG("HTTP request complete, success=%d", success);

    if (!success) {
        GEMINI_LOG("ERROR: HTTP request failed: %s", http_resp.error ? http_resp.error : "unknown");
        snprintf(resp->error, sizeof(resp->error), "HTTP request failed: %s",
                 http_resp.error ? http_resp.error : "unknown");
        resp->result_type = GEMINI_RESULT_ERROR;
        http_response_cleanup(&http_resp);
        return false;
    }

    GEMINI_LOG("HTTP status code: %d", http_resp.status_code);

    // Check HTTP status
    if (http_resp.status_code == 401 || http_resp.status_code == 403) {
        strncpy(resp->error, "Invalid API key", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_INVALID_KEY;
        http_response_cleanup(&http_resp);
        return false;
    }

    if (http_resp.status_code == 429) {
        strncpy(resp->error, "Rate limit exceeded", GEMINI_MAX_ERROR_LEN - 1);
        resp->result_type = GEMINI_RESULT_RATE_LIMIT;
        http_response_cleanup(&http_resp);
        return false;
    }

    if (http_resp.status_code != 200) {
        snprintf(resp->error, sizeof(resp->error), "HTTP error: %d", http_resp.status_code);
        resp->result_type = GEMINI_RESULT_ERROR;
        http_response_cleanup(&http_resp);
        return false;
    }

    // Parse response
    GEMINI_LOG("Parsing response...");
    success = gemini_parse_image_response(http_resp.body, resp);
    http_response_cleanup(&http_resp);

    GEMINI_LOG("=== gemini_edit_image END (success=%d) ===", success);
    return success;
}

bool gemini_generate_edited_path(const char *source_path, char *output_path, size_t output_size)
{
    if (!source_path || !output_path || output_size == 0) return false;

    // Find the extension
    const char *ext = strrchr(source_path, '.');
    const char *filename = strrchr(source_path, '/');
    filename = filename ? filename + 1 : source_path;

    // Find base name length (without extension)
    size_t base_len;
    if (ext && ext > filename) {
        base_len = (size_t)(ext - source_path);
    } else {
        base_len = strlen(source_path);
        ext = "";
    }

    // Try to find a non-existing filename
    for (int version = 1; version <= 999; version++) {
        int written = snprintf(output_path, output_size, "%.*s_edited_%d%s",
                               (int)base_len, source_path, version, ext);
        if (written < 0 || (size_t)written >= output_size) {
            return false;
        }

        // Check if file exists
        FILE *f = fopen(output_path, "r");
        if (!f) {
            // File doesn't exist, use this path
            return true;
        }
        fclose(f);
    }

    return false; // Couldn't find available filename
}

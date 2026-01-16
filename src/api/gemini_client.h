#ifndef GEMINI_CLIENT_H
#define GEMINI_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#define GEMINI_MAX_API_KEY_LEN 256
#define GEMINI_MAX_MODEL_LEN 64
#define GEMINI_MAX_PROMPT_LEN 4096
#define GEMINI_MAX_ERROR_LEN 512
#define GEMINI_MAX_PATH_LEN 4096

// Gemini API endpoints
#define GEMINI_API_BASE_URL "https://generativelanguage.googleapis.com/v1beta/models"
// Image generation models
#define GEMINI_DEFAULT_MODEL "gemini-2.0-flash-exp"
#define GEMINI_QUALITY_MODEL "imagen-3.0-generate-002"

// Debug logging (set to 1 to enable verbose logging, or use -DGEMINI_DEBUG=1)
#ifndef GEMINI_DEBUG
#define GEMINI_DEBUG 0
#endif

// Generation result type
typedef enum GeminiResultType {
    GEMINI_RESULT_SUCCESS = 0,
    GEMINI_RESULT_ERROR,
    GEMINI_RESULT_INVALID_KEY,
    GEMINI_RESULT_RATE_LIMIT,
    GEMINI_RESULT_CONTENT_FILTERED
} GeminiResultType;

// Image format from response
typedef enum GeminiImageFormat {
    GEMINI_FORMAT_PNG = 0,
    GEMINI_FORMAT_JPEG,
    GEMINI_FORMAT_WEBP,
    GEMINI_FORMAT_UNKNOWN
} GeminiImageFormat;

// Image edit request (takes source image + edit prompt)
typedef struct GeminiImageEditRequest {
    char model[GEMINI_MAX_MODEL_LEN];
    char prompt[GEMINI_MAX_PROMPT_LEN];
    char source_image_path[GEMINI_MAX_PATH_LEN];
} GeminiImageEditRequest;

// Image generation request
typedef struct GeminiImageRequest {
    char model[GEMINI_MAX_MODEL_LEN];
    char prompt[GEMINI_MAX_PROMPT_LEN];
    // Reserved for future API features (not yet supported by Gemini)
    int width;
    int height;
} GeminiImageRequest;

// Image generation response
typedef struct GeminiImageResponse {
    GeminiResultType result_type;
    unsigned char *image_data;
    size_t image_size;
    GeminiImageFormat format;
    char mime_type[64];
    char error[GEMINI_MAX_ERROR_LEN];
} GeminiImageResponse;

// Gemini client
typedef struct GeminiClient {
    char api_key[GEMINI_MAX_API_KEY_LEN];
    bool initialized;
    char current_model[GEMINI_MAX_MODEL_LEN];
} GeminiClient;

// Client lifecycle
GeminiClient *gemini_client_create(const char *api_key);
void gemini_client_destroy(GeminiClient *client);
bool gemini_client_is_valid(const GeminiClient *client);
void gemini_client_set_model(GeminiClient *client, const char *model);

// Request functions
void gemini_request_init(GeminiImageRequest *req);
void gemini_request_set_prompt(GeminiImageRequest *req, const char *prompt);
void gemini_request_set_model(GeminiImageRequest *req, const char *model);

// Response functions
void gemini_response_init(GeminiImageResponse *resp);
void gemini_response_cleanup(GeminiImageResponse *resp);

// Generate image
bool gemini_generate_image(GeminiClient *client,
                           const GeminiImageRequest *req,
                           GeminiImageResponse *resp);

// Edit request functions
void gemini_edit_request_init(GeminiImageEditRequest *req);
void gemini_edit_request_set_prompt(GeminiImageEditRequest *req, const char *prompt);
void gemini_edit_request_set_source_image(GeminiImageEditRequest *req, const char *path);
void gemini_edit_request_set_model(GeminiImageEditRequest *req, const char *model);

// Edit image (takes source image and edit prompt, outputs edited image)
bool gemini_edit_image(GeminiClient *client,
                       const GeminiImageEditRequest *req,
                       GeminiImageResponse *resp);

// Generate versioned output path (e.g., image.png -> image_edited_1.png)
bool gemini_generate_edited_path(const char *source_path, char *output_path, size_t output_size);

// Save image to file
bool gemini_save_image(const GeminiImageResponse *resp, const char *path);

// Utility functions
const char *gemini_result_to_string(GeminiResultType result);
const char *gemini_format_to_extension(GeminiImageFormat format);
GeminiImageFormat gemini_format_from_mime(const char *mime_type);

#endif // GEMINI_CLIENT_H

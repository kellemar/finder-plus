#ifndef CLIP_H
#define CLIP_H

#include <stdbool.h>
#include <stddef.h>

// CLIP embedding dimension (ViT-B/32)
#define CLIP_EMBEDDING_DIMENSION 512

// Maximum text query length
#define CLIP_MAX_TEXT_LEN 77  // CLIP tokenizer limit

// CLIP status codes
typedef enum CLIPStatus {
    CLIP_STATUS_OK = 0,
    CLIP_STATUS_NOT_INITIALIZED,
    CLIP_STATUS_MODEL_NOT_FOUND,
    CLIP_STATUS_MODEL_LOAD_ERROR,
    CLIP_STATUS_IMAGE_LOAD_ERROR,
    CLIP_STATUS_IMAGE_FORMAT_ERROR,
    CLIP_STATUS_TEXT_TOO_LONG,
    CLIP_STATUS_INFERENCE_ERROR,
    CLIP_STATUS_MEMORY_ERROR
} CLIPStatus;

// CLIP engine configuration
typedef struct CLIPConfig {
    char model_path[4096];      // Path to GGUF model file
    int num_threads;            // Number of threads (0 = auto)
    bool use_gpu;               // Use GPU acceleration
    int image_size;             // Input image size (default: 224)
} CLIPConfig;

// CLIP engine context (opaque)
typedef struct CLIPEngine CLIPEngine;

// Image embedding result
typedef struct CLIPImageResult {
    float embedding[CLIP_EMBEDDING_DIMENSION];
    CLIPStatus status;
    float inference_time_ms;
    int width;                  // Original image width
    int height;                 // Original image height
} CLIPImageResult;

// Text embedding result
typedef struct CLIPTextResult {
    float embedding[CLIP_EMBEDDING_DIMENSION];
    CLIPStatus status;
    float inference_time_ms;
} CLIPTextResult;

// Batch image embedding result
typedef struct CLIPBatchImageResult {
    float *embeddings;          // Array of embeddings
    int count;
    CLIPStatus status;
    float total_time_ms;
} CLIPBatchImageResult;

// Create CLIP engine with default configuration
CLIPEngine* clip_engine_create(void);

// Create CLIP engine with custom configuration
CLIPEngine* clip_engine_create_with_config(const CLIPConfig *config);

// Destroy CLIP engine
void clip_engine_destroy(CLIPEngine *engine);

// Load CLIP model
CLIPStatus clip_engine_load_model(CLIPEngine *engine, const char *model_path);

// Unload CLIP model
void clip_engine_unload_model(CLIPEngine *engine);

// Check if model is loaded
bool clip_engine_is_loaded(const CLIPEngine *engine);

// Generate embedding for an image file
CLIPImageResult clip_embed_image(CLIPEngine *engine, const char *image_path);

// Generate embedding for image data in memory
CLIPImageResult clip_embed_image_data(CLIPEngine *engine,
                                       const unsigned char *data,
                                       int width,
                                       int height,
                                       int channels);

// Generate embedding for text query
CLIPTextResult clip_embed_text(CLIPEngine *engine, const char *text);

// Batch embed images
CLIPBatchImageResult clip_embed_images_batch(CLIPEngine *engine,
                                              const char **image_paths,
                                              int count);

// Free batch result
void clip_batch_result_free(CLIPBatchImageResult *result);

// Calculate similarity between image and text embeddings
float clip_similarity(const float *image_embedding, const float *text_embedding);

// Check if file is a supported image format
bool clip_is_supported_image(const char *path);

// Get status message
const char* clip_status_message(CLIPStatus status);

// Get default model path
const char* clip_get_default_model_path(void);

// Check if model exists
bool clip_model_exists(const char *model_path);

#endif // CLIP_H

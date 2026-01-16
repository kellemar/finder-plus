#include "clip.h"
#include "ai_common.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

#ifdef FINDER_PLUS_AI_MODELS
#include "clip_wrapper.h"
#endif

// Timing helper macro
#define ELAPSED_MS(start, end) ((float)((end) - (start)) * 1000.0f / CLOCKS_PER_SEC)

// CLIP engine internal structure
struct CLIPEngine {
    CLIPConfig config;
    bool model_loaded;
    bool initialized;
#ifdef FINDER_PLUS_AI_MODELS
    struct clip_ctx *clip_ctx;
    int n_threads;
#endif
};

// Default model path
static const char *DEFAULT_MODEL_PATH = "models/clip-vit-b32.gguf";

// Supported image extensions
static const char * const SUPPORTED_EXTENSIONS[] = {
    "jpg", "jpeg", "png", "bmp", "gif", "webp", "tiff", "tif", NULL
};

// Validate engine is ready for inference
static CLIPStatus validate_engine_ready(const CLIPEngine *engine)
{
    if (engine == NULL || !engine->initialized) {
        return CLIP_STATUS_NOT_INITIALIZED;
    }
    if (!engine->model_loaded) {
        return CLIP_STATUS_NOT_INITIALIZED;
    }
    return CLIP_STATUS_OK;
}

// Generate a deterministic pseudo-random embedding from a hash
// This is a STUB - real implementation uses CLIP model
static void generate_stub_clip_embedding(unsigned long hash, float *output)
{
    generate_stub_embedding_from_hash(hash, output, CLIP_EMBEDDING_DIMENSION);
}

// Hash file path for consistent stub embeddings
static unsigned long hash_file_path(const char *path)
{
    return djb2_hash(path_basename(path));
}

CLIPEngine* clip_engine_create(void)
{
    CLIPConfig config = {0};
    config.num_threads = 0;
    config.use_gpu = false;
    config.image_size = 224;  // CLIP default
    strncpy(config.model_path, DEFAULT_MODEL_PATH, sizeof(config.model_path) - 1);

    return clip_engine_create_with_config(&config);
}

CLIPEngine* clip_engine_create_with_config(const CLIPConfig *config)
{
    if (config == NULL) {
        return NULL;
    }

    CLIPEngine *engine = calloc(1, sizeof(CLIPEngine));
    if (engine == NULL) {
        return NULL;
    }

    memcpy(&engine->config, config, sizeof(CLIPConfig));
    engine->model_loaded = false;
    engine->initialized = true;

#ifdef FINDER_PLUS_AI_MODELS
    engine->clip_ctx = NULL;
    engine->n_threads = config->num_threads > 0 ? config->num_threads : 4;
#endif

    return engine;
}

void clip_engine_destroy(CLIPEngine *engine)
{
    if (engine == NULL) {
        return;
    }

    clip_engine_unload_model(engine);
    free(engine);
}

CLIPStatus clip_engine_load_model(CLIPEngine *engine, const char *model_path)
{
    if (engine == NULL) {
        return CLIP_STATUS_NOT_INITIALIZED;
    }

    if (model_path != NULL) {
        strncpy(engine->config.model_path, model_path, sizeof(engine->config.model_path) - 1);
    }

    if (!clip_model_exists(engine->config.model_path)) {
        return CLIP_STATUS_MODEL_NOT_FOUND;
    }

#ifdef FINDER_PLUS_AI_MODELS
    // Load real CLIP model using clip.cpp
    engine->clip_ctx = clip_model_load(engine->config.model_path, 1);
    if (engine->clip_ctx == NULL) {
        return CLIP_STATUS_MODEL_LOAD_ERROR;
    }
    engine->model_loaded = true;
    return CLIP_STATUS_OK;
#else
    // Stub implementation
    engine->model_loaded = true;
    return CLIP_STATUS_OK;
#endif
}

void clip_engine_unload_model(CLIPEngine *engine)
{
    if (engine == NULL) {
        return;
    }

#ifdef FINDER_PLUS_AI_MODELS
    if (engine->clip_ctx != NULL) {
        clip_free(engine->clip_ctx);
        engine->clip_ctx = NULL;
    }
#endif

    engine->model_loaded = false;
}

bool clip_engine_is_loaded(const CLIPEngine *engine)
{
    if (engine == NULL) {
        return false;
    }
    return engine->model_loaded;
}

CLIPImageResult clip_embed_image(CLIPEngine *engine, const char *image_path)
{
    CLIPImageResult result = {0};
    clock_t start, end;

    result.status = validate_engine_ready(engine);
    if (result.status != CLIP_STATUS_OK) {
        return result;
    }

    if (image_path == NULL) {
        result.status = CLIP_STATUS_IMAGE_LOAD_ERROR;
        return result;
    }

    if (!clip_is_supported_image(image_path)) {
        result.status = CLIP_STATUS_IMAGE_FORMAT_ERROR;
        return result;
    }

    // Check file exists
    struct stat st;
    if (stat(image_path, &st) != 0) {
        result.status = CLIP_STATUS_IMAGE_LOAD_ERROR;
        return result;
    }

    start = clock();

#ifdef FINDER_PLUS_AI_MODELS
    struct clip_image_u8 *img_u8 = NULL;
    struct clip_image_f32 *img_f32 = NULL;

    img_u8 = clip_image_u8_make();
    if (img_u8 == NULL) {
        result.status = CLIP_STATUS_MEMORY_ERROR;
        goto cleanup;
    }

    if (!clip_image_load_from_file(image_path, img_u8)) {
        result.status = CLIP_STATUS_IMAGE_LOAD_ERROR;
        goto cleanup;
    }

    result.width = img_u8->nx;
    result.height = img_u8->ny;

    img_f32 = clip_image_f32_make();
    if (img_f32 == NULL) {
        result.status = CLIP_STATUS_MEMORY_ERROR;
        goto cleanup;
    }

    if (!clip_image_preprocess(engine->clip_ctx, img_u8, img_f32)) {
        result.status = CLIP_STATUS_INFERENCE_ERROR;
        goto cleanup;
    }

    if (!clip_image_encode(engine->clip_ctx, engine->n_threads, img_f32, result.embedding, true)) {
        result.status = CLIP_STATUS_INFERENCE_ERROR;
        goto cleanup;
    }

    result.status = CLIP_STATUS_OK;

cleanup:
    if (img_u8) clip_image_u8_free(img_u8);
    if (img_f32) clip_image_f32_free(img_f32);
#else
    // Stub: generate embedding from file path
    generate_stub_clip_embedding(hash_file_path(image_path), result.embedding);
    result.width = engine->config.image_size;
    result.height = engine->config.image_size;
    result.status = CLIP_STATUS_OK;
#endif

    end = clock();
    result.inference_time_ms = ELAPSED_MS(start, end);

    return result;
}

CLIPImageResult clip_embed_image_data(CLIPEngine *engine,
                                       const unsigned char *data,
                                       int width,
                                       int height,
                                       int channels)
{
    CLIPImageResult result = {0};
    clock_t start, end;

    result.status = validate_engine_ready(engine);
    if (result.status != CLIP_STATUS_OK) {
        return result;
    }

    if (data == NULL || width <= 0 || height <= 0 || channels <= 0) {
        result.status = CLIP_STATUS_IMAGE_LOAD_ERROR;
        return result;
    }

    start = clock();

#ifdef FINDER_PLUS_AI_MODELS
    struct clip_image_u8 *img_u8 = NULL;
    struct clip_image_f32 *img_f32 = NULL;
    uint8_t *rgb_data = NULL;

    // CLIP expects RGB (3 channels), convert if needed
    size_t rgb_size = (size_t)width * (size_t)height * 3;
    rgb_data = malloc(rgb_size);
    if (rgb_data == NULL) {
        result.status = CLIP_STATUS_MEMORY_ERROR;
        goto cleanup_data;
    }

    // Convert input to RGB
    if (channels == 3) {
        memcpy(rgb_data, data, rgb_size);
    } else if (channels == 4) {
        // RGBA to RGB
        for (int i = 0; i < width * height; i++) {
            rgb_data[i * 3 + 0] = data[i * 4 + 0];
            rgb_data[i * 3 + 1] = data[i * 4 + 1];
            rgb_data[i * 3 + 2] = data[i * 4 + 2];
        }
    } else if (channels == 1) {
        // Grayscale to RGB
        for (int i = 0; i < width * height; i++) {
            rgb_data[i * 3 + 0] = data[i];
            rgb_data[i * 3 + 1] = data[i];
            rgb_data[i * 3 + 2] = data[i];
        }
    } else {
        result.status = CLIP_STATUS_IMAGE_FORMAT_ERROR;
        goto cleanup_data;
    }

    img_u8 = clip_image_u8_make();
    if (img_u8 == NULL) {
        result.status = CLIP_STATUS_MEMORY_ERROR;
        goto cleanup_data;
    }

    // Set image data (clip_image_u8_free will not free this, we manage it)
    img_u8->nx = width;
    img_u8->ny = height;
    img_u8->size = rgb_size;
    img_u8->data = rgb_data;
    rgb_data = NULL;  // Ownership transferred to img_u8

    result.width = width;
    result.height = height;

    img_f32 = clip_image_f32_make();
    if (img_f32 == NULL) {
        result.status = CLIP_STATUS_MEMORY_ERROR;
        goto cleanup_data;
    }

    if (!clip_image_preprocess(engine->clip_ctx, img_u8, img_f32)) {
        result.status = CLIP_STATUS_INFERENCE_ERROR;
        goto cleanup_data;
    }

    if (!clip_image_encode(engine->clip_ctx, engine->n_threads, img_f32, result.embedding, true)) {
        result.status = CLIP_STATUS_INFERENCE_ERROR;
        goto cleanup_data;
    }

    result.status = CLIP_STATUS_OK;

cleanup_data:
    free(rgb_data);  // Safe if NULL
    if (img_u8) {
        free(img_u8->data);  // Free the manually allocated data
        img_u8->data = NULL;
        clip_image_u8_free(img_u8);
    }
    if (img_f32) clip_image_f32_free(img_f32);
#else
    // Stub: generate embedding from image dimensions
    unsigned long hash = (unsigned long)width * 31 + (unsigned long)height * 17 + (unsigned long)channels;
    generate_stub_clip_embedding(hash, result.embedding);
    result.width = width;
    result.height = height;
    result.status = CLIP_STATUS_OK;
#endif

    end = clock();
    result.inference_time_ms = ELAPSED_MS(start, end);

    return result;
}

CLIPTextResult clip_embed_text(CLIPEngine *engine, const char *text)
{
    CLIPTextResult result = {0};
    clock_t start, end;

    result.status = validate_engine_ready(engine);
    if (result.status != CLIP_STATUS_OK) {
        return result;
    }

    if (text == NULL) {
        result.status = CLIP_STATUS_INFERENCE_ERROR;
        return result;
    }

    size_t text_len = strlen(text);
    if (text_len > CLIP_MAX_TEXT_LEN) {
        result.status = CLIP_STATUS_TEXT_TOO_LONG;
        return result;
    }

    start = clock();

#ifdef FINDER_PLUS_AI_MODELS
    struct clip_tokens tokens = {0};

    tokens.data = malloc(CLIP_MAX_TEXT_LEN * sizeof(clip_vocab_id));
    if (tokens.data == NULL) {
        result.status = CLIP_STATUS_MEMORY_ERROR;
        goto cleanup_text;
    }

    if (!clip_tokenize(engine->clip_ctx, text, &tokens)) {
        result.status = CLIP_STATUS_INFERENCE_ERROR;
        goto cleanup_text;
    }

    if (!clip_text_encode(engine->clip_ctx, engine->n_threads, &tokens, result.embedding, true)) {
        result.status = CLIP_STATUS_INFERENCE_ERROR;
        goto cleanup_text;
    }

    result.status = CLIP_STATUS_OK;

cleanup_text:
    free(tokens.data);
#else
    // Stub: generate embedding from text hash
    generate_stub_clip_embedding(djb2_hash(text), result.embedding);
    result.status = CLIP_STATUS_OK;
#endif

    end = clock();
    result.inference_time_ms = ELAPSED_MS(start, end);

    return result;
}

CLIPBatchImageResult clip_embed_images_batch(CLIPEngine *engine,
                                              const char **image_paths,
                                              int count)
{
    CLIPBatchImageResult result = {0};

    result.status = validate_engine_ready(engine);
    if (result.status != CLIP_STATUS_OK) {
        return result;
    }

    if (image_paths == NULL || count <= 0) {
        result.status = CLIP_STATUS_IMAGE_LOAD_ERROR;
        return result;
    }

    result.embeddings = calloc((size_t)count * CLIP_EMBEDDING_DIMENSION, sizeof(float));
    if (result.embeddings == NULL) {
        result.status = CLIP_STATUS_MEMORY_ERROR;
        return result;
    }

    clock_t start = clock();

    for (int i = 0; i < count; i++) {
        if (image_paths[i] == NULL || !clip_is_supported_image(image_paths[i])) {
            continue;
        }

        // Use single-image function for real embeddings
        CLIPImageResult img_result = clip_embed_image(engine, image_paths[i]);
        if (img_result.status == CLIP_STATUS_OK) {
            memcpy(&result.embeddings[result.count * CLIP_EMBEDDING_DIMENSION],
                   img_result.embedding,
                   CLIP_EMBEDDING_DIMENSION * sizeof(float));
            result.count++;
        }
    }

    clock_t end = clock();
    result.total_time_ms = ELAPSED_MS(start, end);
    result.status = CLIP_STATUS_OK;

    return result;
}

void clip_batch_result_free(CLIPBatchImageResult *result)
{
    if (result == NULL) {
        return;
    }

    if (result->embeddings != NULL) {
        free(result->embeddings);
        result->embeddings = NULL;
    }
    result->count = 0;
}

float clip_similarity(const float *image_embedding, const float *text_embedding)
{
    return cosine_similarity(image_embedding, text_embedding, CLIP_EMBEDDING_DIMENSION);
}

bool clip_is_supported_image(const char *path)
{
    if (path == NULL) {
        return false;
    }

    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return false;
    }
    ext++;  // Skip the dot

    // Convert to lowercase for comparison
    char ext_lower[16] = {0};
    for (int i = 0; i < 15 && ext[i]; i++) {
        ext_lower[i] = (char)tolower((unsigned char)ext[i]);
    }

    for (int i = 0; SUPPORTED_EXTENSIONS[i] != NULL; i++) {
        if (strcmp(ext_lower, SUPPORTED_EXTENSIONS[i]) == 0) {
            return true;
        }
    }

    return false;
}

const char* clip_status_message(CLIPStatus status)
{
    switch (status) {
        case CLIP_STATUS_OK:
            return "Success";
        case CLIP_STATUS_NOT_INITIALIZED:
            return "CLIP engine not initialized";
        case CLIP_STATUS_MODEL_NOT_FOUND:
            return "CLIP model file not found";
        case CLIP_STATUS_MODEL_LOAD_ERROR:
            return "Failed to load CLIP model";
        case CLIP_STATUS_IMAGE_LOAD_ERROR:
            return "Failed to load image";
        case CLIP_STATUS_IMAGE_FORMAT_ERROR:
            return "Unsupported image format";
        case CLIP_STATUS_TEXT_TOO_LONG:
            return "Text query exceeds maximum length";
        case CLIP_STATUS_INFERENCE_ERROR:
            return "Inference error";
        case CLIP_STATUS_MEMORY_ERROR:
            return "Memory allocation error";
        default:
            return "Unknown error";
    }
}

const char* clip_get_default_model_path(void)
{
    return DEFAULT_MODEL_PATH;
}

bool clip_model_exists(const char *model_path)
{
    if (model_path == NULL) {
        return false;
    }

    struct stat st;
    return stat(model_path, &st) == 0 && S_ISREG(st.st_mode);
}

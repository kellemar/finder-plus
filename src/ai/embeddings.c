#include "embeddings.h"
#include "ai_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>

#ifdef FINDER_PLUS_AI_MODELS
#include "bert_wrapper.h"
#endif

// Embedding engine internal structure
struct EmbeddingEngine {
    EmbeddingConfig config;
    bool model_loaded;
    bool initialized;
#ifdef FINDER_PLUS_AI_MODELS
    struct bert_ctx *bert_ctx;
    int n_threads;
#endif
};

// Default model path relative to executable or home directory
static const char *DEFAULT_MODEL_PATH = "models/all-MiniLM-L6-v2/ggml-model-q4_0.bin";

EmbeddingEngine* embedding_engine_create(void)
{
    EmbeddingConfig config = {0};
    config.num_threads = 0;  // Auto-detect
    config.use_gpu = false;
    config.batch_size = 32;
    strncpy(config.model_path, DEFAULT_MODEL_PATH, sizeof(config.model_path) - 1);

    return embedding_engine_create_with_config(&config);
}

EmbeddingEngine* embedding_engine_create_with_config(const EmbeddingConfig *config)
{
    if (config == NULL) {
        return NULL;
    }

    EmbeddingEngine *engine = calloc(1, sizeof(EmbeddingEngine));
    if (engine == NULL) {
        return NULL;
    }

    memcpy(&engine->config, config, sizeof(EmbeddingConfig));
    engine->model_loaded = false;
    engine->initialized = true;

#ifdef FINDER_PLUS_AI_MODELS
    engine->bert_ctx = NULL;
    // Auto-detect threads if not specified
    engine->n_threads = config->num_threads > 0 ? config->num_threads : 4;
#endif

    return engine;
}

void embedding_engine_destroy(EmbeddingEngine *engine)
{
    if (engine == NULL) {
        return;
    }

    embedding_engine_unload_model(engine);
    free(engine);
}

EmbeddingStatus embedding_engine_load_model(EmbeddingEngine *engine, const char *model_path)
{
    if (engine == NULL) {
        return EMBEDDING_STATUS_NOT_INITIALIZED;
    }

    if (model_path != NULL) {
        strncpy(engine->config.model_path, model_path, sizeof(engine->config.model_path) - 1);
    }

    // Check if model file exists
    if (!embedding_model_exists(engine->config.model_path)) {
        return EMBEDDING_STATUS_MODEL_NOT_FOUND;
    }

#ifdef FINDER_PLUS_AI_MODELS
    // Unload any existing model
    if (engine->bert_ctx != NULL) {
        bert_free(engine->bert_ctx);
        engine->bert_ctx = NULL;
    }

    // Load bert.cpp model
    engine->bert_ctx = bert_load_from_file(engine->config.model_path);
    if (engine->bert_ctx == NULL) {
        return EMBEDDING_STATUS_MODEL_LOAD_ERROR;
    }

    // Verify embedding dimension matches expected
    int32_t embd_dim = bert_n_embd(engine->bert_ctx);
    if (embd_dim != EMBEDDING_DIMENSION) {
        fprintf(stderr, "Warning: Model embedding dimension (%d) differs from expected (%d)\n",
                embd_dim, EMBEDDING_DIMENSION);
    }

    engine->model_loaded = true;
    return EMBEDDING_STATUS_OK;
#else
    // Stub mode: pretend to load the model
    engine->model_loaded = true;
    return EMBEDDING_STATUS_OK;
#endif
}

void embedding_engine_unload_model(EmbeddingEngine *engine)
{
    if (engine == NULL) {
        return;
    }

#ifdef FINDER_PLUS_AI_MODELS
    if (engine->bert_ctx != NULL) {
        bert_free(engine->bert_ctx);
        engine->bert_ctx = NULL;
    }
#endif

    engine->model_loaded = false;
}

bool embedding_engine_is_loaded(const EmbeddingEngine *engine)
{
    if (engine == NULL) {
        return false;
    }
    return engine->model_loaded;
}

// Generate a deterministic pseudo-random embedding based on text hash
// This is a STUB - used as fallback when real models are not available
static void generate_stub_embedding(const char *text, float *output)
{
    generate_stub_embedding_from_hash(djb2_hash(text), output, EMBEDDING_DIMENSION);
}

// Normalize embedding vector to unit length
static void normalize_embedding(float *embedding, int dimension)
{
    float norm = 0.0f;
    for (int i = 0; i < dimension; i++) {
        norm += embedding[i] * embedding[i];
    }
    norm = sqrtf(norm);
    if (norm > 1e-6f) {
        for (int i = 0; i < dimension; i++) {
            embedding[i] /= norm;
        }
    }
}

EmbeddingResult embedding_generate(EmbeddingEngine *engine, const char *text)
{
    EmbeddingResult result = {0};

    if (engine == NULL || !engine->initialized) {
        result.status = EMBEDDING_STATUS_NOT_INITIALIZED;
        return result;
    }

    if (!engine->model_loaded) {
        result.status = EMBEDDING_STATUS_NOT_INITIALIZED;
        return result;
    }

    if (text == NULL) {
        result.status = EMBEDDING_STATUS_INFERENCE_ERROR;
        return result;
    }

    size_t text_len = strlen(text);
    if (text_len > EMBEDDING_MAX_TEXT_LEN) {
        result.status = EMBEDDING_STATUS_TEXT_TOO_LONG;
        return result;
    }

    // Measure inference time
    clock_t start = clock();

#ifdef FINDER_PLUS_AI_MODELS
    if (engine->bert_ctx != NULL) {
        // Real bert.cpp inference
        bert_encode(engine->bert_ctx, engine->n_threads, text, result.embedding);
        // Normalize to unit vector for consistent cosine similarity
        normalize_embedding(result.embedding, EMBEDDING_DIMENSION);
    } else {
        // Fallback to stub
        generate_stub_embedding(text, result.embedding);
    }
#else
    // Stub mode
    generate_stub_embedding(text, result.embedding);
#endif

    clock_t end = clock();
    result.inference_time_ms = (float)(end - start) * 1000.0f / CLOCKS_PER_SEC;
    result.status = EMBEDDING_STATUS_OK;

    return result;
}

BatchEmbeddingResult embedding_generate_batch(EmbeddingEngine *engine,
                                               const char **texts,
                                               int count)
{
    BatchEmbeddingResult result = {0};

    if (engine == NULL || !engine->initialized) {
        result.status = EMBEDDING_STATUS_NOT_INITIALIZED;
        return result;
    }

    if (!engine->model_loaded) {
        result.status = EMBEDDING_STATUS_NOT_INITIALIZED;
        return result;
    }

    if (texts == NULL || count <= 0) {
        result.status = EMBEDDING_STATUS_INFERENCE_ERROR;
        return result;
    }

    // Allocate memory for all embeddings
    result.embeddings = calloc((size_t)count * EMBEDDING_DIMENSION, sizeof(float));
    if (result.embeddings == NULL) {
        result.status = EMBEDDING_STATUS_MEMORY_ERROR;
        return result;
    }

    clock_t start = clock();

#ifdef FINDER_PLUS_AI_MODELS
    if (engine->bert_ctx != NULL) {
        // Allocate array of embedding pointers for bert_encode_batch
        float **emb_ptrs = malloc(count * sizeof(float*));
        if (emb_ptrs == NULL) {
            free(result.embeddings);
            result.embeddings = NULL;
            result.status = EMBEDDING_STATUS_MEMORY_ERROR;
            return result;
        }

        // Set up pointers to each embedding slot
        for (int i = 0; i < count; i++) {
            emb_ptrs[i] = &result.embeddings[i * EMBEDDING_DIMENSION];
        }

        // Use bert.cpp batch encoding
        int batch_size = engine->config.batch_size > 0 ? engine->config.batch_size : 32;
        bert_encode_batch(engine->bert_ctx, engine->n_threads, batch_size, count, texts, emb_ptrs);

        // Normalize all embeddings
        for (int i = 0; i < count; i++) {
            if (texts[i] != NULL) {
                normalize_embedding(emb_ptrs[i], EMBEDDING_DIMENSION);
                result.count++;
            }
        }

        free(emb_ptrs);
    } else {
        // Fallback to stub
        for (int i = 0; i < count; i++) {
            if (texts[i] == NULL) continue;
            size_t text_len = strlen(texts[i]);
            if (text_len > EMBEDDING_MAX_TEXT_LEN) continue;
            generate_stub_embedding(texts[i], &result.embeddings[i * EMBEDDING_DIMENSION]);
            result.count++;
        }
    }
#else
    // Stub mode: generate embeddings for each text
    for (int i = 0; i < count; i++) {
        if (texts[i] == NULL) {
            continue;
        }

        size_t text_len = strlen(texts[i]);
        if (text_len > EMBEDDING_MAX_TEXT_LEN) {
            continue;
        }

        generate_stub_embedding(texts[i], &result.embeddings[i * EMBEDDING_DIMENSION]);
        result.count++;
    }
#endif

    clock_t end = clock();
    result.total_time_ms = (float)(end - start) * 1000.0f / CLOCKS_PER_SEC;
    result.status = EMBEDDING_STATUS_OK;

    return result;
}

void batch_embedding_result_free(BatchEmbeddingResult *result)
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

float embedding_cosine_similarity(const float *a, const float *b)
{
    return cosine_similarity(a, b, EMBEDDING_DIMENSION);
}

const char* embedding_status_message(EmbeddingStatus status)
{
    switch (status) {
        case EMBEDDING_STATUS_OK:
            return "Success";
        case EMBEDDING_STATUS_NOT_INITIALIZED:
            return "Embedding engine not initialized";
        case EMBEDDING_STATUS_MODEL_NOT_FOUND:
            return "Model file not found";
        case EMBEDDING_STATUS_MODEL_LOAD_ERROR:
            return "Failed to load model";
        case EMBEDDING_STATUS_TEXT_TOO_LONG:
            return "Input text exceeds maximum length";
        case EMBEDDING_STATUS_INFERENCE_ERROR:
            return "Inference error";
        case EMBEDDING_STATUS_MEMORY_ERROR:
            return "Memory allocation error";
        default:
            return "Unknown error";
    }
}

const char* embedding_get_default_model_path(void)
{
    return DEFAULT_MODEL_PATH;
}

bool embedding_model_exists(const char *model_path)
{
    if (model_path == NULL) {
        return false;
    }

    struct stat st;
    return stat(model_path, &st) == 0 && S_ISREG(st.st_mode);
}

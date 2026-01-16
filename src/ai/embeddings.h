#ifndef EMBEDDINGS_H
#define EMBEDDINGS_H

#include <stdbool.h>
#include <stddef.h>

// Embedding dimension for all-MiniLM-L6-v2 model
#define EMBEDDING_DIMENSION 384

// Maximum text length for embedding (in characters)
#define EMBEDDING_MAX_TEXT_LEN 8192

// Embedding engine status
typedef enum EmbeddingStatus {
    EMBEDDING_STATUS_OK = 0,
    EMBEDDING_STATUS_NOT_INITIALIZED,
    EMBEDDING_STATUS_MODEL_NOT_FOUND,
    EMBEDDING_STATUS_MODEL_LOAD_ERROR,
    EMBEDDING_STATUS_TEXT_TOO_LONG,
    EMBEDDING_STATUS_INFERENCE_ERROR,
    EMBEDDING_STATUS_MEMORY_ERROR
} EmbeddingStatus;

// Embedding engine configuration
typedef struct EmbeddingConfig {
    char model_path[4096];      // Path to GGUF model file
    int num_threads;            // Number of threads for inference (0 = auto)
    bool use_gpu;               // Use GPU acceleration if available
    int batch_size;             // Batch size for batch embedding
} EmbeddingConfig;

// Embedding engine context (opaque)
typedef struct EmbeddingEngine EmbeddingEngine;

// Embedding result
typedef struct EmbeddingResult {
    float embedding[EMBEDDING_DIMENSION];   // The embedding vector
    EmbeddingStatus status;                 // Status of the operation
    float inference_time_ms;                // Time taken for inference
} EmbeddingResult;

// Batch embedding result
typedef struct BatchEmbeddingResult {
    float *embeddings;          // Array of embeddings (count * EMBEDDING_DIMENSION floats)
    int count;                  // Number of embeddings generated
    EmbeddingStatus status;     // Overall status
    float total_time_ms;        // Total time for batch
} BatchEmbeddingResult;

// Initialize embedding engine with default configuration
EmbeddingEngine* embedding_engine_create(void);

// Initialize embedding engine with custom configuration
EmbeddingEngine* embedding_engine_create_with_config(const EmbeddingConfig *config);

// Destroy embedding engine and free resources
void embedding_engine_destroy(EmbeddingEngine *engine);

// Load model (lazy loading - call before first embedding)
EmbeddingStatus embedding_engine_load_model(EmbeddingEngine *engine, const char *model_path);

// Unload model to free memory
void embedding_engine_unload_model(EmbeddingEngine *engine);

// Check if model is loaded
bool embedding_engine_is_loaded(const EmbeddingEngine *engine);

// Generate embedding for a single text
EmbeddingResult embedding_generate(EmbeddingEngine *engine, const char *text);

// Generate embeddings for multiple texts (batch processing)
BatchEmbeddingResult embedding_generate_batch(EmbeddingEngine *engine,
                                               const char **texts,
                                               int count);

// Free batch embedding result
void batch_embedding_result_free(BatchEmbeddingResult *result);

// Calculate cosine similarity between two embeddings
float embedding_cosine_similarity(const float *a, const float *b);

// Get status message for error code
const char* embedding_status_message(EmbeddingStatus status);

// Get default model path
const char* embedding_get_default_model_path(void);

// Check if the default model exists
bool embedding_model_exists(const char *model_path);

#endif // EMBEDDINGS_H

// C-compatible wrapper for bert.cpp
// This header provides C99-compatible declarations for bert.cpp functions

#ifndef BERT_WRAPPER_H
#define BERT_WRAPPER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque context type
struct bert_ctx;

// Load model from file
struct bert_ctx* bert_load_from_file(const char* fname);

// Free model context
void bert_free(struct bert_ctx* ctx);

// Generate embedding for a single text
void bert_encode(
    struct bert_ctx* ctx,
    int32_t n_threads,
    const char* text,
    float* embeddings);

// Generate embeddings for multiple texts (batch)
// n_batch_size - how many to process at a time
// n_inputs     - total size of texts and embeddings arrays
void bert_encode_batch(
    struct bert_ctx* ctx,
    int32_t n_threads,
    int32_t n_batch_size,
    int32_t n_inputs,
    const char** texts,
    float** embeddings);

// Get embedding dimension
int32_t bert_n_embd(struct bert_ctx* ctx);

// Get maximum token count
int32_t bert_n_max_tokens(struct bert_ctx* ctx);

#ifdef __cplusplus
}
#endif

#endif // BERT_WRAPPER_H

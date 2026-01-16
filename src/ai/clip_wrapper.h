// C-compatible wrapper for clip.cpp
// This header provides declarations without needing ggml.h

#ifndef CLIP_WRAPPER_H
#define CLIP_WRAPPER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations (opaque types)
struct clip_ctx;

// Token structure
typedef int32_t clip_vocab_id;
struct clip_tokens {
    clip_vocab_id *data;
    size_t size;
};

// RGB uint8 image
struct clip_image_u8 {
    int nx;
    int ny;
    uint8_t *data;
    size_t size;
};

// RGB float32 image (preprocessed)
struct clip_image_f32 {
    int nx;
    int ny;
    float *data;
    size_t size;
};

// Model loading and management
struct clip_ctx* clip_model_load(const char *fname, const int verbosity);
void clip_free(struct clip_ctx *ctx);

// Image memory management
struct clip_image_u8* clip_image_u8_make(void);
struct clip_image_f32* clip_image_f32_make(void);
void clip_image_u8_free(struct clip_image_u8 *img);
void clip_image_f32_free(struct clip_image_f32 *res);
void clip_image_u8_clean(struct clip_image_u8 *img);
void clip_image_f32_clean(struct clip_image_f32 *res);

// Image loading and preprocessing
bool clip_image_load_from_file(const char *fname, struct clip_image_u8 *img);
bool clip_image_preprocess(const struct clip_ctx *ctx, const struct clip_image_u8 *img,
                           struct clip_image_f32 *res);

// Encoding functions
bool clip_image_encode(const struct clip_ctx *ctx, const int n_threads,
                       struct clip_image_f32 *img, float *vec, const bool normalize);
bool clip_tokenize(const struct clip_ctx *ctx, const char *text, struct clip_tokens *tokens);
bool clip_text_encode(const struct clip_ctx *ctx, const int n_threads,
                      const struct clip_tokens *tokens, float *vec, const bool normalize);

// Utility functions
float clip_similarity_score(const float *vec1, const float *vec2, const int vec_dim);
bool clip_compare_text_and_image(const struct clip_ctx *ctx, const int n_threads,
                                 const char *text, const struct clip_image_u8 *image,
                                 float *score);

#ifdef __cplusplus
}
#endif

#endif // CLIP_WRAPPER_H

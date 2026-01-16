#ifndef AI_COMMON_H
#define AI_COMMON_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AI_EPSILON 1e-4f

// Generic cosine similarity calculation
static inline float cosine_similarity(const float *a, const float *b, int dimension)
{
    if (a == NULL || b == NULL) {
        return 0.0f;
    }

    float dot = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;

    for (int i = 0; i < dimension; i++) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    float denom = sqrtf(norm_a) * sqrtf(norm_b);
    if (denom < AI_EPSILON) {
        return 0.0f;
    }

    return dot / denom;
}

// DJB2 string hash function
static inline unsigned long djb2_hash(const char *str)
{
    unsigned long hash = 5381;
    while (*str) {
        hash = ((hash << 5) + hash) + (unsigned char)(*str++);
    }
    return hash;
}

// Generate a deterministic pseudo-random embedding from a hash
// This is a STUB for testing - real implementation will use actual models
static inline void generate_stub_embedding_from_hash(unsigned long hash, float *output, int dimension)
{
    srand((unsigned int)hash);
    float norm = 0.0f;

    for (int i = 0; i < dimension; i++) {
        output[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        norm += output[i] * output[i];
    }

    norm = sqrtf(norm);
    if (norm > AI_EPSILON) {
        for (int i = 0; i < dimension; i++) {
            output[i] /= norm;
        }
    }
}

// Get basename from path
static inline const char* path_basename(const char *path)
{
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

#endif // AI_COMMON_H

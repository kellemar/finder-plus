#ifndef VISUAL_SEARCH_H
#define VISUAL_SEARCH_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "clip.h"
#include "vectordb.h"

// Maximum query length for visual search
#define VISUAL_SEARCH_MAX_QUERY_LEN 256

// Visual search result
typedef struct VisualSearchResult {
    char path[4096];
    char name[256];
    float score;            // Similarity score (0-1)
    int width;              // Image width
    int height;             // Image height
    int64_t size;           // File size
} VisualSearchResult;

// Visual search results
typedef struct VisualSearchResults {
    VisualSearchResult *results;
    int count;
    int capacity;
    float search_time_ms;
    char query[VISUAL_SEARCH_MAX_QUERY_LEN];
    bool success;
    char error_message[256];
} VisualSearchResults;

// Visual search options
typedef struct VisualSearchOptions {
    int max_results;           // Maximum number of results (default: 20)
    float min_score;           // Minimum similarity score (default: 0.0)
    const char *directory;     // Limit search to this directory (NULL for all)
} VisualSearchOptions;

// Image index entry
typedef struct ImageIndexEntry {
    char path[4096];
    float embedding[CLIP_EMBEDDING_DIMENSION];
    int width;
    int height;
    int64_t size;
    time_t modified_time;
} ImageIndexEntry;

// Visual search context (opaque)
typedef struct VisualSearch VisualSearch;

// Create visual search context
VisualSearch* visual_search_create(void);

// Destroy visual search context
void visual_search_destroy(VisualSearch *vs);

// Set the CLIP engine (required)
void visual_search_set_clip_engine(VisualSearch *vs, CLIPEngine *engine);

// Set the vector database (required)
void visual_search_set_vectordb(VisualSearch *vs, VectorDB *db);

// Search for images matching a text query
VisualSearchResults visual_search_query(VisualSearch *vs,
                                         const char *query,
                                         const VisualSearchOptions *options);

// Search for similar images given an image path
VisualSearchResults visual_search_similar(VisualSearch *vs,
                                           const char *image_path,
                                           const VisualSearchOptions *options);

// Index a single image (add to database)
bool visual_search_index_image(VisualSearch *vs, const char *image_path);

// Index all images in a directory (recursive)
int visual_search_index_directory(VisualSearch *vs, const char *directory);

// Free search results
void visual_search_results_free(VisualSearchResults *results);

// Get default search options
VisualSearchOptions visual_search_default_options(void);

// Check if visual search is ready
bool visual_search_is_ready(const VisualSearch *vs);

// Get statistics
typedef struct VisualSearchStats {
    int64_t total_images;
    int64_t indexed_images;
    bool engine_loaded;
} VisualSearchStats;

VisualSearchStats visual_search_get_stats(const VisualSearch *vs);

#endif // VISUAL_SEARCH_H

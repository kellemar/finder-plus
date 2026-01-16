#ifndef SEMANTIC_SEARCH_H
#define SEMANTIC_SEARCH_H

#include <stdbool.h>
#include <stdint.h>
#include "embeddings.h"
#include "vectordb.h"
#include "indexer.h"

// Maximum query length
#define SEMANTIC_SEARCH_MAX_QUERY_LEN 1024

// Search result structure
typedef struct SemanticSearchResult {
    char path[4096];
    char name[256];
    IndexedFileType file_type;
    int64_t size;
    float score;           // Similarity score (0-1)
} SemanticSearchResult;

// Search results array
typedef struct SemanticSearchResults {
    SemanticSearchResult *results;
    int count;
    int capacity;
    float search_time_ms;
    char query[SEMANTIC_SEARCH_MAX_QUERY_LEN];
    bool success;
    char error_message[256];
} SemanticSearchResults;

// Search options
typedef struct SemanticSearchOptions {
    int max_results;           // Maximum number of results (default: 20)
    float min_score;           // Minimum similarity score (default: 0.0)
    const char *directory;     // Limit search to this directory (NULL for all)
    IndexedFileType file_type; // Filter by file type (FILE_TYPE_UNKNOWN for all)
    bool sort_by_score;        // Sort by score (default: true)
} SemanticSearchOptions;

// Semantic search context (opaque)
typedef struct SemanticSearch SemanticSearch;

// Create semantic search context
SemanticSearch* semantic_search_create(void);

// Destroy semantic search context
void semantic_search_destroy(SemanticSearch *search);

// Set the embedding engine (required)
void semantic_search_set_embedding_engine(SemanticSearch *search, EmbeddingEngine *engine);

// Set the vector database (required)
void semantic_search_set_vectordb(SemanticSearch *search, VectorDB *db);

// Set the indexer (optional - for automatic reindexing)
void semantic_search_set_indexer(SemanticSearch *search, Indexer *indexer);

// Perform semantic search with query text
SemanticSearchResults semantic_search_query(SemanticSearch *search,
                                             const char *query,
                                             const SemanticSearchOptions *options);

// Perform semantic search with pre-computed embedding
SemanticSearchResults semantic_search_by_embedding(SemanticSearch *search,
                                                    const float *embedding,
                                                    const SemanticSearchOptions *options);

// Find files similar to a given file
SemanticSearchResults semantic_search_similar_to_file(SemanticSearch *search,
                                                       const char *file_path,
                                                       const SemanticSearchOptions *options);

// Free search results
void semantic_search_results_free(SemanticSearchResults *results);

// Get default search options
SemanticSearchOptions semantic_search_default_options(void);

// Check if semantic search is ready (engine loaded, db connected)
bool semantic_search_is_ready(const SemanticSearch *search);

// Get index statistics
typedef struct SemanticSearchStats {
    int64_t total_files;
    int64_t files_with_embeddings;
    int64_t total_size_bytes;
    bool indexer_running;
    float indexer_progress;
} SemanticSearchStats;

SemanticSearchStats semantic_search_get_stats(const SemanticSearch *search);

#endif // SEMANTIC_SEARCH_H

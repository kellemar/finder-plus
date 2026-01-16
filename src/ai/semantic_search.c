#include "semantic_search.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Semantic search internal structure
struct SemanticSearch {
    EmbeddingEngine *embedding_engine;
    VectorDB *vectordb;
    Indexer *indexer;
    bool initialized;
};

SemanticSearch* semantic_search_create(void)
{
    SemanticSearch *search = calloc(1, sizeof(SemanticSearch));
    if (search == NULL) {
        return NULL;
    }

    search->initialized = true;
    return search;
}

void semantic_search_destroy(SemanticSearch *search)
{
    if (search == NULL) {
        return;
    }

    // We don't own the engine, db, or indexer - just clear refs
    search->embedding_engine = NULL;
    search->vectordb = NULL;
    search->indexer = NULL;

    free(search);
}

void semantic_search_set_embedding_engine(SemanticSearch *search, EmbeddingEngine *engine)
{
    if (search == NULL) {
        return;
    }
    search->embedding_engine = engine;
}

void semantic_search_set_vectordb(SemanticSearch *search, VectorDB *db)
{
    if (search == NULL) {
        return;
    }
    search->vectordb = db;
}

void semantic_search_set_indexer(SemanticSearch *search, Indexer *indexer)
{
    if (search == NULL) {
        return;
    }
    search->indexer = indexer;
}

SemanticSearchOptions semantic_search_default_options(void)
{
    SemanticSearchOptions options = {
        .max_results = 20,
        .min_score = 0.0f,
        .directory = NULL,
        .file_type = FILE_TYPE_UNKNOWN,
        .sort_by_score = true
    };
    return options;
}

static SemanticSearchResults create_error_result(const char *error)
{
    SemanticSearchResults results = {0};
    results.success = false;
    strncpy(results.error_message, error, sizeof(results.error_message) - 1);
    return results;
}

// Helper function to get time in milliseconds
static float get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0f + ts.tv_nsec / 1000000.0f;
}

SemanticSearchResults semantic_search_query(SemanticSearch *search,
                                             const char *query,
                                             const SemanticSearchOptions *options)
{
    if (search == NULL || !search->initialized) {
        return create_error_result("Semantic search not initialized");
    }

    if (search->embedding_engine == NULL) {
        return create_error_result("No embedding engine set");
    }

    if (!embedding_engine_is_loaded(search->embedding_engine)) {
        return create_error_result("Embedding model not loaded");
    }

    if (search->vectordb == NULL) {
        return create_error_result("No vector database set");
    }

    if (query == NULL || query[0] == '\0') {
        return create_error_result("Query is empty");
    }

    // Use default options if not provided
    SemanticSearchOptions opts;
    if (options != NULL) {
        opts = *options;
    } else {
        opts = semantic_search_default_options();
    }

    float start_time = get_time_ms();

    // Generate query embedding
    EmbeddingResult embed_result = embedding_generate(search->embedding_engine, query);
    if (embed_result.status != EMBEDDING_STATUS_OK) {
        return create_error_result(embedding_status_message(embed_result.status));
    }

    // Perform search
    SemanticSearchResults results = semantic_search_by_embedding(search, embed_result.embedding, &opts);

    // Store query and timing
    strncpy(results.query, query, sizeof(results.query) - 1);
    results.search_time_ms = get_time_ms() - start_time;

    return results;
}

SemanticSearchResults semantic_search_by_embedding(SemanticSearch *search,
                                                    const float *embedding,
                                                    const SemanticSearchOptions *options)
{
    SemanticSearchResults results = {0};

    if (search == NULL || !search->initialized) {
        return create_error_result("Semantic search not initialized");
    }

    if (search->vectordb == NULL) {
        return create_error_result("No vector database set");
    }

    if (embedding == NULL) {
        return create_error_result("Embedding is NULL");
    }

    // Use default options if not provided
    SemanticSearchOptions opts;
    if (options != NULL) {
        opts = *options;
    } else {
        opts = semantic_search_default_options();
    }

    float start_time = get_time_ms();

    // Perform vector search
    VectorSearchResults vresults;
    if (opts.directory != NULL) {
        vresults = vectordb_search_in_directory(search->vectordb, embedding,
                                                 opts.directory, opts.max_results);
    } else {
        vresults = vectordb_search(search->vectordb, embedding, opts.max_results);
    }

    if (vresults.status != VECTORDB_STATUS_OK) {
        return create_error_result(vectordb_status_message(vresults.status));
    }

    // Convert to semantic search results with filtering
    results.capacity = vresults.count;
    results.results = calloc((size_t)vresults.count, sizeof(SemanticSearchResult));
    if (results.results == NULL) {
        vector_search_results_free(&vresults);
        return create_error_result("Memory allocation error");
    }

    for (int i = 0; i < vresults.count; i++) {
        VectorSearchResult *vr = &vresults.results[i];

        // Apply filters
        if (vr->similarity < opts.min_score) {
            continue;
        }

        if (opts.file_type != FILE_TYPE_UNKNOWN && vr->file.file_type != opts.file_type) {
            continue;
        }

        // Add to results
        SemanticSearchResult *sr = &results.results[results.count];
        strncpy(sr->path, vr->file.path, sizeof(sr->path) - 1);
        strncpy(sr->name, vr->file.name, sizeof(sr->name) - 1);
        sr->file_type = vr->file.file_type;
        sr->size = vr->file.size;
        sr->score = vr->similarity;
        results.count++;
    }

    vector_search_results_free(&vresults);

    results.search_time_ms = get_time_ms() - start_time;
    results.success = true;

    return results;
}

SemanticSearchResults semantic_search_similar_to_file(SemanticSearch *search,
                                                       const char *file_path,
                                                       const SemanticSearchOptions *options)
{
    if (search == NULL || !search->initialized) {
        return create_error_result("Semantic search not initialized");
    }

    if (search->vectordb == NULL) {
        return create_error_result("No vector database set");
    }

    if (file_path == NULL) {
        return create_error_result("File path is NULL");
    }

    // Get the file's embedding from the database
    IndexedFile file;
    VectorDBStatus status = vectordb_get_file(search->vectordb, file_path, &file);

    if (status != VECTORDB_STATUS_OK) {
        return create_error_result("File not found in index");
    }

    if (!file.has_embedding) {
        return create_error_result("File has no embedding");
    }

    // Search using the file's embedding
    SemanticSearchResults results = semantic_search_by_embedding(search, file.embedding, options);

    // Remove the query file itself from results
    for (int i = 0; i < results.count; i++) {
        if (strcmp(results.results[i].path, file_path) == 0) {
            // Shift remaining results
            for (int j = i; j < results.count - 1; j++) {
                results.results[j] = results.results[j + 1];
            }
            results.count--;
            break;
        }
    }

    return results;
}

void semantic_search_results_free(SemanticSearchResults *results)
{
    if (results == NULL) {
        return;
    }

    if (results->results != NULL) {
        free(results->results);
        results->results = NULL;
    }
    results->count = 0;
    results->capacity = 0;
}

bool semantic_search_is_ready(const SemanticSearch *search)
{
    if (search == NULL || !search->initialized) {
        return false;
    }

    if (search->embedding_engine == NULL || !embedding_engine_is_loaded(search->embedding_engine)) {
        return false;
    }

    if (search->vectordb == NULL) {
        return false;
    }

    return true;
}

SemanticSearchStats semantic_search_get_stats(const SemanticSearch *search)
{
    SemanticSearchStats stats = {0};

    if (search == NULL || !search->initialized) {
        return stats;
    }

    if (search->vectordb != NULL) {
        stats.total_files = vectordb_count_files(search->vectordb);
        stats.total_size_bytes = vectordb_total_size(search->vectordb);
        // Note: files_with_embeddings requires additional query
        stats.files_with_embeddings = stats.total_files;  // Approximation
    }

    if (search->indexer != NULL) {
        IndexerStats istats = indexer_get_stats(search->indexer);
        stats.indexer_running = indexer_is_busy(search->indexer);
        stats.indexer_progress = istats.progress;
    }

    return stats;
}

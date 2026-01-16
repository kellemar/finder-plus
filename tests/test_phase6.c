#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../src/ai/duplicates.h"
#include "../src/ai/smart_rename.h"
#include "../src/ai/organization.h"
#include "../src/ai/summarize.h"
#include "../src/ai/nl_operations.h"

// Test framework macros from test_main.c
extern void inc_tests_run(void);
extern void inc_tests_passed(void);
extern void inc_tests_failed(void);

#define TEST_ASSERT(condition, message) do { \
    inc_tests_run(); \
    if (condition) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s (line %d)\n", message, __LINE__); \
    } \
} while(0)

// Helper to create test directory structure
static char test_dir[256];

static void setup_test_dir(void)
{
    snprintf(test_dir, sizeof(test_dir), "/tmp/finder_plus_test_%d", getpid());
    mkdir(test_dir, 0755);
}

static void cleanup_test_dir(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", test_dir);
    system(cmd);
}

static void create_test_file(const char *name, const char *content)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_dir, name);
    FILE *f = fopen(path, "w");
    if (f) {
        if (content) fputs(content, f);
        fclose(f);
    }
}

static void create_test_subdir(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_dir, name);
    mkdir(path, 0755);
}

// =============================================================================
// Duplicate Detection Tests
// =============================================================================

static void test_duplicates_config_init(void)
{
    DuplicateConfig config;
    duplicate_config_init(&config);

    TEST_ASSERT(config.detect_exact == true, "Default detect_exact should be true");
    TEST_ASSERT(config.detect_similar_images == true, "Default detect_similar_images should be true");
    TEST_ASSERT(config.detect_similar_text == true, "Default detect_similar_text should be true");
    TEST_ASSERT(config.similarity_threshold >= 0.85f && config.similarity_threshold <= 0.95f,
                "Default similarity_threshold should be 0.85-0.95");
    TEST_ASSERT(config.recursive == true, "Default recursive should be true");
}

static void test_duplicates_analysis_create_free(void)
{
    DuplicateAnalysis *analysis = duplicate_analysis_create();
    TEST_ASSERT(analysis != NULL, "Should create analysis");
    TEST_ASSERT(analysis->groups != NULL, "Should allocate groups array");
    TEST_ASSERT(analysis->group_count == 0, "Initial group count should be 0");

    duplicate_analysis_free(analysis);
    TEST_ASSERT(true, "Should free analysis without crash");
}

static void test_duplicates_hash_file_md5(void)
{
    setup_test_dir();
    create_test_file("test1.txt", "Hello, World!");
    create_test_file("test2.txt", "Hello, World!");  // Same content
    create_test_file("test3.txt", "Different content");

    char path1[512], path2[512], path3[512];
    snprintf(path1, sizeof(path1), "%s/test1.txt", test_dir);
    snprintf(path2, sizeof(path2), "%s/test2.txt", test_dir);
    snprintf(path3, sizeof(path3), "%s/test3.txt", test_dir);

    uint8_t hash1[HASH_SIZE_MD5], hash2[HASH_SIZE_MD5], hash3[HASH_SIZE_MD5];

    TEST_ASSERT(hash_file_md5(path1, hash1), "Should hash file1");
    TEST_ASSERT(hash_file_md5(path2, hash2), "Should hash file2");
    TEST_ASSERT(hash_file_md5(path3, hash3), "Should hash file3");

    TEST_ASSERT(memcmp(hash1, hash2, HASH_SIZE_MD5) == 0, "Same content should have same hash");
    TEST_ASSERT(memcmp(hash1, hash3, HASH_SIZE_MD5) != 0, "Different content should have different hash");

    cleanup_test_dir();
}

static void test_duplicates_files_are_identical(void)
{
    setup_test_dir();
    create_test_file("same1.txt", "Identical content here");
    create_test_file("same2.txt", "Identical content here");
    create_test_file("diff.txt", "Different content here!");

    char path1[512], path2[512], path3[512];
    snprintf(path1, sizeof(path1), "%s/same1.txt", test_dir);
    snprintf(path2, sizeof(path2), "%s/same2.txt", test_dir);
    snprintf(path3, sizeof(path3), "%s/diff.txt", test_dir);

    TEST_ASSERT(files_are_identical(path1, path2), "Same content files should be identical");
    TEST_ASSERT(!files_are_identical(path1, path3), "Different content files should not be identical");

    cleanup_test_dir();
}

static void test_duplicates_hamming_distance(void)
{
    uint8_t hash1[8] = {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    uint8_t hash2[8] = {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    uint8_t hash3[8] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00};

    TEST_ASSERT(hash_hamming_distance(hash1, hash2, 8) == 0, "Identical hashes should have distance 0");
    TEST_ASSERT(hash_hamming_distance(hash1, hash3, 8) == 8, "One byte difference should have distance 8");
}

static void test_duplicates_scan_directory(void)
{
    setup_test_dir();
    create_test_file("dup1.txt", "This is duplicate content");
    create_test_file("dup2.txt", "This is duplicate content");
    create_test_file("unique.txt", "This is unique content");

    DuplicateConfig config;
    duplicate_config_init(&config);
    config.detect_similar_images = false;
    config.detect_similar_text = false;

    DuplicateAnalysis *analysis = duplicate_analysis_create();
    DuplicateStatus status = duplicate_scan_directory(test_dir, &config, NULL, NULL, analysis);

    TEST_ASSERT(status == DUP_STATUS_OK, "Scan should succeed");
    TEST_ASSERT(analysis->total_files_scanned == 3, "Should scan 3 files");
    TEST_ASSERT(analysis->group_count >= 1, "Should find at least 1 duplicate group");

    duplicate_analysis_free(analysis);
    cleanup_test_dir();
}

static void test_duplicates_status_message(void)
{
    TEST_ASSERT(strcmp(duplicate_status_message(DUP_STATUS_OK), "OK") == 0,
                "OK status message correct");
    TEST_ASSERT(strcmp(duplicate_status_message(DUP_STATUS_CANCELLED), "Cancelled") == 0,
                "Cancelled status message correct");
}

// =============================================================================
// Smart Rename Tests
// =============================================================================

static void test_smart_rename_config_init(void)
{
    SmartRenameConfig config;
    smart_rename_config_init(&config);

    TEST_ASSERT(config.default_format == RENAME_FORMAT_SNAKE_CASE,
                "Default format should be snake_case");
    TEST_ASSERT(config.auto_detect_format == true, "Auto detect format should be true");
    TEST_ASSERT(config.min_confidence >= 0.0f && config.min_confidence <= 1.0f,
                "Min confidence should be between 0 and 1");
}

static void test_smart_rename_request_create_free(void)
{
    BatchRenameRequest *request = smart_rename_request_create();
    TEST_ASSERT(request != NULL, "Should create request");
    TEST_ASSERT(request->suggestions != NULL, "Should allocate suggestions array");
    TEST_ASSERT(request->count == 0, "Initial count should be 0");

    smart_rename_request_free(request);
    TEST_ASSERT(true, "Should free request without crash");
}

static void test_smart_rename_format_name(void)
{
    char output[256];

    smart_rename_format_name("HelloWorld", RENAME_FORMAT_SNAKE_CASE, output, sizeof(output));
    TEST_ASSERT(strcmp(output, "hello_world") == 0, "Should convert to snake_case");

    smart_rename_format_name("hello_world", RENAME_FORMAT_KEBAB_CASE, output, sizeof(output));
    TEST_ASSERT(strcmp(output, "hello-world") == 0, "Should convert to kebab-case");

    smart_rename_format_name("hello world", RENAME_FORMAT_CAMEL_CASE, output, sizeof(output));
    TEST_ASSERT(strcmp(output, "helloWorld") == 0, "Should convert to camelCase");

    smart_rename_format_name("hello world", RENAME_FORMAT_PASCAL_CASE, output, sizeof(output));
    TEST_ASSERT(strcmp(output, "HelloWorld") == 0, "Should convert to PascalCase");

    smart_rename_format_name("hello_world", RENAME_FORMAT_TITLE_CASE, output, sizeof(output));
    TEST_ASSERT(strcmp(output, "Hello World") == 0, "Should convert to Title Case");
}

static void test_smart_rename_add_file(void)
{
    setup_test_dir();
    create_test_file("document.pdf", "");
    create_test_file("image.jpg", "");

    BatchRenameRequest *request = smart_rename_request_create();

    char path1[512], path2[512];
    snprintf(path1, sizeof(path1), "%s/document.pdf", test_dir);
    snprintf(path2, sizeof(path2), "%s/image.jpg", test_dir);

    TEST_ASSERT(smart_rename_request_add_file(request, path1), "Should add first file");
    TEST_ASSERT(smart_rename_request_add_file(request, path2), "Should add second file");
    TEST_ASSERT(request->count == 2, "Request should have 2 files");

    TEST_ASSERT(strcmp(request->suggestions[0].original_name, "document") == 0,
                "First file name should be 'document'");
    TEST_ASSERT(strcmp(request->suggestions[0].extension, ".pdf") == 0,
                "First file extension should be '.pdf'");

    smart_rename_request_free(request);
    cleanup_test_dir();
}

static void test_smart_rename_expand_pattern(void)
{
    setup_test_dir();
    create_test_file("test.txt", "content");

    char path[512];
    snprintf(path, sizeof(path), "%s/test.txt", test_dir);

    char output[256];

    smart_rename_expand_pattern("%name%", path, 1, output, sizeof(output));
    TEST_ASSERT(strcmp(output, "test") == 0, "Should expand %name%");

    smart_rename_expand_pattern("%index%", path, 5, output, sizeof(output));
    TEST_ASSERT(strcmp(output, "005") == 0, "Should expand %index% with padding");

    smart_rename_expand_pattern("file_%name%_%index%", path, 3, output, sizeof(output));
    TEST_ASSERT(strcmp(output, "file_test_003") == 0, "Should expand multiple patterns");

    cleanup_test_dir();
}

static void test_smart_rename_status_message(void)
{
    TEST_ASSERT(strcmp(smart_rename_status_message(RENAME_STATUS_OK), "OK") == 0,
                "OK status message correct");
    TEST_ASSERT(strcmp(smart_rename_status_message(RENAME_STATUS_CONFLICT), "Name conflict") == 0,
                "Conflict status message correct");
}

// =============================================================================
// Organization Tests
// =============================================================================

static void test_organization_config_init(void)
{
    OrganizationConfig config;
    organization_config_init(&config);

    TEST_ASSERT(config.detect_duplicates == true, "Default detect_duplicates should be true");
    TEST_ASSERT(config.old_file_days == 365, "Default old_file_days should be 365");
    TEST_ASSERT(config.create_subfolders == true, "Default create_subfolders should be true");
}

static void test_organization_analysis_create_free(void)
{
    OrganizationAnalysis *analysis = organization_analysis_create();
    TEST_ASSERT(analysis != NULL, "Should create analysis");
    TEST_ASSERT(analysis->files != NULL, "Should allocate files array");
    TEST_ASSERT(analysis->file_count == 0, "Initial file count should be 0");

    organization_analysis_free(analysis);
    TEST_ASSERT(true, "Should free analysis without crash");
}

static void test_organization_category_name(void)
{
    TEST_ASSERT(strcmp(organization_category_name(CAT_DOCUMENTS), "Documents") == 0,
                "Documents category name correct");
    TEST_ASSERT(strcmp(organization_category_name(CAT_IMAGES), "Images") == 0,
                "Images category name correct");
    TEST_ASSERT(strcmp(organization_category_name(CAT_CODE), "Code") == 0,
                "Code category name correct");
}

static void test_organization_analyze(void)
{
    setup_test_dir();
    create_test_file("document.pdf", "PDF content");
    create_test_file("photo.jpg", "\xFF\xD8\xFF");  // JPEG magic bytes
    create_test_file("code.py", "print('hello')");
    create_test_file("data.json", "{}");

    OrganizationConfig config;
    organization_config_init(&config);

    OrganizationAnalysis *analysis = organization_analysis_create();
    OrganizationStatus status = organization_analyze(test_dir, &config, NULL, NULL, analysis);

    TEST_ASSERT(status == ORG_STATUS_OK, "Analysis should succeed");
    TEST_ASSERT(analysis->total_files == 4, "Should analyze 4 files");
    TEST_ASSERT(analysis->category_count > 0, "Should have at least one category");
    TEST_ASSERT(analysis->folder_count > 0, "Should suggest at least one folder");

    organization_analysis_free(analysis);
    cleanup_test_dir();
}

static void test_organization_preview_plan(void)
{
    setup_test_dir();
    create_test_file("test.pdf", "");

    OrganizationConfig config;
    organization_config_init(&config);

    OrganizationAnalysis *analysis = organization_analysis_create();
    organization_analyze(test_dir, &config, NULL, NULL, analysis);

    char *plan = organization_preview_plan(analysis);
    TEST_ASSERT(plan != NULL, "Should generate plan");
    TEST_ASSERT(strstr(plan, "Organization Plan") != NULL, "Plan should have header");
    TEST_ASSERT(strstr(plan, "Total files") != NULL, "Plan should show total files");

    free(plan);
    organization_analysis_free(analysis);
    cleanup_test_dir();
}

static void test_organization_status_message(void)
{
    TEST_ASSERT(strcmp(organization_status_message(ORG_STATUS_OK), "OK") == 0,
                "OK status message correct");
    TEST_ASSERT(strcmp(organization_status_message(ORG_STATUS_FILE_ERROR), "File error") == 0,
                "File error status message correct");
}

// =============================================================================
// Summarize Tests
// =============================================================================

static void test_summarize_config_init(void)
{
    SummarizeConfig config;
    summarize_config_init(&config);

    TEST_ASSERT(config.default_level == SUMM_LEVEL_STANDARD,
                "Default level should be STANDARD");
    TEST_ASSERT(config.use_cache == true, "Default use_cache should be true");
    TEST_ASSERT(config.max_file_size > 0, "Default max_file_size should be positive");
}

static void test_summarize_detect_file_type(void)
{
    TEST_ASSERT(summarize_detect_file_type("test.c") == SUMM_TYPE_CODE,
                "Should detect C as code");
    TEST_ASSERT(summarize_detect_file_type("test.py") == SUMM_TYPE_CODE,
                "Should detect Python as code");
    TEST_ASSERT(summarize_detect_file_type("test.md") == SUMM_TYPE_MARKDOWN,
                "Should detect markdown");
    TEST_ASSERT(summarize_detect_file_type("test.json") == SUMM_TYPE_DATA,
                "Should detect JSON as data");
    TEST_ASSERT(summarize_detect_file_type("test.pdf") == SUMM_TYPE_DOCUMENT,
                "Should detect PDF as document");
    TEST_ASSERT(summarize_detect_file_type("test.xyz") == SUMM_TYPE_UNKNOWN,
                "Should return unknown for unrecognized extension");
}

static void test_summarize_is_supported(void)
{
    TEST_ASSERT(summarize_is_supported("file.txt"), "TXT should be supported");
    TEST_ASSERT(summarize_is_supported("file.py"), "Python should be supported");
    TEST_ASSERT(summarize_is_supported("file.md"), "Markdown should be supported");
    TEST_ASSERT(!summarize_is_supported("file.xyz"), "Unknown extension should not be supported");
}

static void test_summarize_file_type_name(void)
{
    TEST_ASSERT(strcmp(summarize_file_type_name(SUMM_TYPE_CODE), "Code") == 0,
                "Code type name correct");
    TEST_ASSERT(strcmp(summarize_file_type_name(SUMM_TYPE_DOCUMENT), "Document") == 0,
                "Document type name correct");
}

static void test_summarize_status_message(void)
{
    TEST_ASSERT(strcmp(summarize_status_message(SUMM_STATUS_OK), "OK") == 0,
                "OK status message correct");
    TEST_ASSERT(strcmp(summarize_status_message(SUMM_STATUS_TOO_LARGE), "File too large") == 0,
                "Too large status message correct");
}

// =============================================================================
// Natural Language Operations Tests
// =============================================================================

static void test_nl_operations_config_init(void)
{
    NLOperationsConfig config;
    nl_operations_config_init(&config);

    TEST_ASSERT(config.require_confirmation_for_risk == true,
                "Default require_confirmation should be true");
    TEST_ASSERT(config.enable_undo == true, "Default enable_undo should be true");
    TEST_ASSERT(config.confirmation_threshold == NL_RISK_MEDIUM,
                "Default confirmation threshold should be MEDIUM");
}

static void test_nl_undo_history_init(void)
{
    NLUndoHistory history;
    nl_undo_history_init(&history);

    TEST_ASSERT(history.count == 0, "Initial undo count should be 0");
    TEST_ASSERT(history.head == 0, "Initial undo head should be 0");
}

static void test_nl_get_operation_type(void)
{
    TEST_ASSERT(nl_get_operation_type("file_list") == NL_OP_FILE_LIST,
                "Should identify file_list");
    TEST_ASSERT(nl_get_operation_type("file_move") == NL_OP_FILE_MOVE,
                "Should identify file_move");
    TEST_ASSERT(nl_get_operation_type("file_delete") == NL_OP_FILE_DELETE,
                "Should identify file_delete");
    TEST_ASSERT(nl_get_operation_type("unknown_tool") == NL_OP_UNKNOWN,
                "Should return unknown for unrecognized tool");
}

static void test_nl_get_risk_level(void)
{
    TEST_ASSERT(nl_get_risk_level(NL_OP_FILE_LIST) == NL_RISK_NONE,
                "File list should be no risk");
    TEST_ASSERT(nl_get_risk_level(NL_OP_FILE_COPY) == NL_RISK_LOW,
                "File copy should be low risk");
    TEST_ASSERT(nl_get_risk_level(NL_OP_FILE_MOVE) == NL_RISK_MEDIUM,
                "File move should be medium risk");
    TEST_ASSERT(nl_get_risk_level(NL_OP_FILE_DELETE) == NL_RISK_HIGH,
                "File delete should be high risk");
}

static void test_nl_operation_type_name(void)
{
    TEST_ASSERT(strcmp(nl_operation_type_name(NL_OP_FILE_LIST), "List Files") == 0,
                "List Files name correct");
    TEST_ASSERT(strcmp(nl_operation_type_name(NL_OP_FILE_DELETE), "Delete File") == 0,
                "Delete File name correct");
}

static void test_nl_risk_level_name(void)
{
    TEST_ASSERT(strcmp(nl_risk_level_name(NL_RISK_NONE), "Safe") == 0,
                "Safe risk name correct");
    TEST_ASSERT(strcmp(nl_risk_level_name(NL_RISK_HIGH), "High Risk") == 0,
                "High Risk name correct");
}

static void test_nl_status_message(void)
{
    TEST_ASSERT(strcmp(nl_status_message(NL_STATUS_OK), "OK") == 0,
                "OK status message correct");
    TEST_ASSERT(strcmp(nl_status_message(NL_STATUS_CANCELLED), "Cancelled") == 0,
                "Cancelled status message correct");
}

static void test_nl_can_undo(void)
{
    NLUndoHistory history;
    nl_undo_history_init(&history);

    TEST_ASSERT(!nl_can_undo(&history), "Empty history should not allow undo");
}

static void test_nl_confirm_reject(void)
{
    NLOperationChain chain = {0};
    chain.count = 2;
    chain.operations[0].requires_confirmation = true;
    chain.operations[1].requires_confirmation = true;

    nl_confirm_operation(&chain, 0);
    TEST_ASSERT(chain.operations[0].confirmed == true, "Should confirm operation 0");
    TEST_ASSERT(chain.operations[1].confirmed == false, "Operation 1 should still be unconfirmed");

    nl_confirm_all(&chain);
    TEST_ASSERT(chain.operations[0].confirmed == true, "All operations should be confirmed");
    TEST_ASSERT(chain.operations[1].confirmed == true, "All operations should be confirmed");

    nl_reject_all(&chain);
    TEST_ASSERT(chain.operations[0].confirmed == false, "All operations should be rejected");
    TEST_ASSERT(chain.operations[1].confirmed == false, "All operations should be rejected");
}

static void test_nl_generate_preview(void)
{
    NLOperationChain chain = {0};
    strncpy(chain.original_command, "move files to Documents", sizeof(chain.original_command) - 1);
    chain.count = 1;
    chain.operations[0].type = NL_OP_FILE_MOVE;
    chain.operations[0].risk = NL_RISK_MEDIUM;
    strncpy(chain.operations[0].description, "Move file operation", 255);

    NLOperationPreview *preview = nl_generate_preview(&chain);
    TEST_ASSERT(preview != NULL, "Should generate preview");
    TEST_ASSERT(strstr(preview->summary, "move files") != NULL, "Preview should contain command");
    TEST_ASSERT(preview->chain == &chain, "Preview should reference chain");

    nl_preview_free(preview);
}

// =============================================================================
// Main Test Entry Point
// =============================================================================

void test_phase6(void)
{
    printf("\n--- Duplicate Detection Tests ---\n");
    test_duplicates_config_init();
    test_duplicates_analysis_create_free();
    test_duplicates_hash_file_md5();
    test_duplicates_files_are_identical();
    test_duplicates_hamming_distance();
    test_duplicates_scan_directory();
    test_duplicates_status_message();

    printf("\n--- Smart Rename Tests ---\n");
    test_smart_rename_config_init();
    test_smart_rename_request_create_free();
    test_smart_rename_format_name();
    test_smart_rename_add_file();
    test_smart_rename_expand_pattern();
    test_smart_rename_status_message();

    printf("\n--- Organization Tests ---\n");
    test_organization_config_init();
    test_organization_analysis_create_free();
    test_organization_category_name();
    test_organization_analyze();
    test_organization_preview_plan();
    test_organization_status_message();

    printf("\n--- Summarize Tests ---\n");
    test_summarize_config_init();
    test_summarize_detect_file_type();
    test_summarize_is_supported();
    test_summarize_file_type_name();
    test_summarize_status_message();

    printf("\n--- Natural Language Operations Tests ---\n");
    test_nl_operations_config_init();
    test_nl_undo_history_init();
    test_nl_get_operation_type();
    test_nl_get_risk_level();
    test_nl_operation_type_name();
    test_nl_risk_level_name();
    test_nl_status_message();
    test_nl_can_undo();
    test_nl_confirm_reject();
    test_nl_generate_preview();
}

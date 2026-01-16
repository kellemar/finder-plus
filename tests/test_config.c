#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

// Test helper functions
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

#define TEST_ASSERT_EQ(expected, actual, message) do { \
    inc_tests_run(); \
    if ((expected) == (actual)) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s - expected %d, got %d (line %d)\n", message, (int)(expected), (int)(actual), __LINE__); \
    } \
} while(0)

#define TEST_ASSERT_STR_EQ(expected, actual, message) do { \
    inc_tests_run(); \
    if (strcmp((expected), (actual)) == 0) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s - expected '%s', got '%s' (line %d)\n", message, (expected), (actual), __LINE__); \
    } \
} while(0)

// Simple JSON reading helpers (copy of core logic for testing)
static char* json_find_key(const char *json, const char *key)
{
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);

    char *pos = strstr(json, search);
    if (!pos) return NULL;

    pos = strchr(pos, ':');
    if (!pos) return NULL;

    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;

    return pos;
}

static int json_read_int(const char *json, const char *key, int default_value)
{
    char *pos = json_find_key(json, key);
    if (!pos) return default_value;

    int value;
    if (sscanf(pos, "%d", &value) == 1) {
        return value;
    }
    return default_value;
}

static bool json_read_bool(const char *json, const char *key, bool default_value)
{
    char *pos = json_find_key(json, key);
    if (!pos) return default_value;

    if (strncmp(pos, "true", 4) == 0) return true;
    if (strncmp(pos, "false", 5) == 0) return false;
    return default_value;
}

static void json_read_string(const char *json, const char *key, char *out, int max_len, const char *default_value)
{
    char *pos = json_find_key(json, key);
    if (!pos || *pos != '"') {
        strncpy(out, default_value, max_len - 1);
        out[max_len - 1] = '\0';
        return;
    }

    pos++;
    char *end = strchr(pos, '"');
    if (!end) {
        strncpy(out, default_value, max_len - 1);
        out[max_len - 1] = '\0';
        return;
    }

    int len = end - pos;
    if (len >= max_len) len = max_len - 1;

    strncpy(out, pos, len);
    out[len] = '\0';
}

void test_config(void)
{
    // Test JSON integer parsing
    {
        const char *json = "{ \"width\": 1280, \"height\": 720 }";

        int width = json_read_int(json, "width", 0);
        TEST_ASSERT_EQ(1280, width, "Should parse width=1280");

        int height = json_read_int(json, "height", 0);
        TEST_ASSERT_EQ(720, height, "Should parse height=720");

        int missing = json_read_int(json, "missing", 100);
        TEST_ASSERT_EQ(100, missing, "Missing key should return default");
    }

    // Test JSON boolean parsing
    {
        const char *json = "{ \"enabled\": true, \"disabled\": false }";

        bool enabled = json_read_bool(json, "enabled", false);
        TEST_ASSERT(enabled == true, "Should parse enabled=true");

        bool disabled = json_read_bool(json, "disabled", true);
        TEST_ASSERT(disabled == false, "Should parse disabled=false");

        bool missing = json_read_bool(json, "missing", true);
        TEST_ASSERT(missing == true, "Missing bool should return default");
    }

    // Test JSON string parsing
    {
        const char *json = "{ \"name\": \"test_value\", \"path\": \"/home/user\" }";
        char buffer[256];

        json_read_string(json, "name", buffer, sizeof(buffer), "");
        TEST_ASSERT_STR_EQ("test_value", buffer, "Should parse name string");

        json_read_string(json, "path", buffer, sizeof(buffer), "");
        TEST_ASSERT_STR_EQ("/home/user", buffer, "Should parse path string");

        json_read_string(json, "missing", buffer, sizeof(buffer), "default");
        TEST_ASSERT_STR_EQ("default", buffer, "Missing string should return default");
    }

    // Test JSON with whitespace
    {
        const char *json = "{\n  \"value\"  :  42  ,\n  \"flag\" : true\n}";

        int value = json_read_int(json, "value", 0);
        TEST_ASSERT_EQ(42, value, "Should parse with extra whitespace");

        bool flag = json_read_bool(json, "flag", false);
        TEST_ASSERT(flag == true, "Should parse bool with whitespace");
    }

    // Test nested-like structure (flat parsing)
    {
        const char *json = "{ \"window_width\": 1920, \"window_height\": 1080 }";

        int width = json_read_int(json, "window_width", 0);
        TEST_ASSERT_EQ(1920, width, "Should parse prefixed key");
    }

    // Test empty string
    {
        char buffer[256];
        const char *json = "{ \"empty\": \"\" }";

        json_read_string(json, "empty", buffer, sizeof(buffer), "default");
        TEST_ASSERT_STR_EQ("", buffer, "Should parse empty string");
    }

    // Test negative numbers
    {
        const char *json = "{ \"x\": -100, \"y\": -50 }";

        int x = json_read_int(json, "x", 0);
        TEST_ASSERT_EQ(-100, x, "Should parse negative x");

        int y = json_read_int(json, "y", 0);
        TEST_ASSERT_EQ(-50, y, "Should parse negative y");
    }

    // Test string truncation
    {
        const char *json = "{ \"long\": \"this_is_a_very_long_string_that_should_be_truncated\" }";
        char buffer[10];

        json_read_string(json, "long", buffer, sizeof(buffer), "");
        TEST_ASSERT(strlen(buffer) < 10, "String should be truncated to buffer size");
        TEST_ASSERT(strncmp(buffer, "this_is_a", 9) == 0, "Truncated string should preserve start");
    }

    // Test malformed JSON (graceful handling)
    {
        const char *json = "{ broken";

        int value = json_read_int(json, "value", 42);
        TEST_ASSERT_EQ(42, value, "Malformed JSON should return default");
    }
}

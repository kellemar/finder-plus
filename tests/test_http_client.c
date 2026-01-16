#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api/http_client.h"

// Test macros from test_main.c
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

static void test_client_create_destroy(void)
{
    HttpClient *client = http_client_create();
    TEST_ASSERT(client != NULL, "Create HTTP client");
    TEST_ASSERT(client->initialized == true, "Client is initialized");

    http_client_destroy(client);
    printf("  PASS: Destroy HTTP client (no crash)\n");
    inc_tests_run();
    inc_tests_passed();
}

static void test_client_timeout(void)
{
    HttpClient *client = http_client_create();

    // Default timeouts
    TEST_ASSERT(client->timeout_connect == HTTP_TIMEOUT_CONNECT, "Default connect timeout");
    TEST_ASSERT(client->timeout_transfer == HTTP_TIMEOUT_TRANSFER, "Default transfer timeout");

    // Custom timeouts
    http_client_set_timeout(client, 10, 60);
    TEST_ASSERT(client->timeout_connect == 10, "Custom connect timeout");
    TEST_ASSERT(client->timeout_transfer == 60, "Custom transfer timeout");

    // Invalid timeout (should use defaults)
    http_client_set_timeout(client, -1, -1);
    TEST_ASSERT(client->timeout_connect == HTTP_TIMEOUT_CONNECT, "Invalid timeout uses default");

    http_client_destroy(client);
}

static void test_request_init(void)
{
    HttpRequest req;
    http_request_init(&req);

    TEST_ASSERT(req.method == HTTP_GET, "Default method is GET");
    TEST_ASSERT(req.url[0] == '\0', "URL is empty");
    TEST_ASSERT(req.header_count == 0, "No headers");
    TEST_ASSERT(req.body == NULL, "Body is NULL");
    TEST_ASSERT(req.body_len == 0, "Body length is 0");
}

static void test_request_method(void)
{
    HttpRequest req;
    http_request_init(&req);

    http_request_set_method(&req, HTTP_POST);
    TEST_ASSERT(req.method == HTTP_POST, "Set method to POST");

    http_request_set_method(&req, HTTP_PUT);
    TEST_ASSERT(req.method == HTTP_PUT, "Set method to PUT");

    http_request_set_method(&req, HTTP_DELETE);
    TEST_ASSERT(req.method == HTTP_DELETE, "Set method to DELETE");

    TEST_ASSERT(strcmp(http_method_to_string(HTTP_GET), "GET") == 0, "GET string");
    TEST_ASSERT(strcmp(http_method_to_string(HTTP_POST), "POST") == 0, "POST string");
    TEST_ASSERT(strcmp(http_method_to_string(HTTP_PUT), "PUT") == 0, "PUT string");
    TEST_ASSERT(strcmp(http_method_to_string(HTTP_DELETE), "DELETE") == 0, "DELETE string");
}

static void test_request_url(void)
{
    HttpRequest req;
    http_request_init(&req);

    http_request_set_url(&req, "https://api.example.com/v1/test");
    TEST_ASSERT(strcmp(req.url, "https://api.example.com/v1/test") == 0, "Set URL");

    // Test URL truncation for very long URLs
    char long_url[HTTP_MAX_URL_LEN + 100];
    memset(long_url, 'a', sizeof(long_url) - 1);
    long_url[sizeof(long_url) - 1] = '\0';

    http_request_set_url(&req, long_url);
    TEST_ASSERT(strlen(req.url) == HTTP_MAX_URL_LEN - 1, "Long URL is truncated");
}

static void test_request_headers(void)
{
    HttpRequest req;
    http_request_init(&req);

    http_request_add_header(&req, "Content-Type", "application/json");
    TEST_ASSERT(req.header_count == 1, "Header count is 1");
    TEST_ASSERT(strstr(req.headers[0], "Content-Type: application/json") != NULL, "Header formatted correctly");

    http_request_add_header(&req, "Authorization", "Bearer token123");
    TEST_ASSERT(req.header_count == 2, "Header count is 2");

    http_request_add_header(&req, "X-Custom", "value");
    TEST_ASSERT(req.header_count == 3, "Header count is 3");
}

static void test_request_body(void)
{
    HttpRequest req;
    http_request_init(&req);

    const char *body = "{\"key\": \"value\"}";
    http_request_set_body_string(&req, body);
    TEST_ASSERT(req.body != NULL, "Body is set");
    TEST_ASSERT(strcmp(req.body, body) == 0, "Body content is correct");
    TEST_ASSERT(req.body_len == strlen(body), "Body length is correct");

    // Replace body
    const char *new_body = "new content";
    http_request_set_body_string(&req, new_body);
    TEST_ASSERT(strcmp(req.body, new_body) == 0, "Body replaced");
    TEST_ASSERT(req.body_len == strlen(new_body), "New body length");

    http_request_cleanup(&req);
    TEST_ASSERT(req.body == NULL, "Body cleaned up");
    TEST_ASSERT(req.body_len == 0, "Body length reset");
}

static void test_response_init(void)
{
    HttpResponse resp;
    http_response_init(&resp);

    TEST_ASSERT(resp.status_code == 0, "Status code is 0");
    TEST_ASSERT(resp.body == NULL, "Body is NULL");
    TEST_ASSERT(resp.body_len == 0, "Body length is 0");
    TEST_ASSERT(resp.error == NULL, "Error is NULL");
}

static void test_response_cleanup(void)
{
    HttpResponse resp;
    http_response_init(&resp);

    // Simulate populated response
    resp.body = strdup("Response body");
    resp.body_len = strlen(resp.body);
    resp.error = strdup("Some error");
    resp.status_code = 200;

    http_response_cleanup(&resp);
    TEST_ASSERT(resp.body == NULL, "Body cleaned up");
    TEST_ASSERT(resp.error == NULL, "Error cleaned up");
    TEST_ASSERT(resp.status_code == 0, "Status code reset");
    TEST_ASSERT(resp.body_len == 0, "Body length reset");
}

static void test_real_http_request(void)
{
    // Test with a real HTTP request to a reliable endpoint
    HttpClient *client = http_client_create();
    if (!client) {
        printf("  SKIP: Could not create HTTP client\n");
        inc_tests_run();
        return;
    }

    HttpRequest req;
    http_request_init(&req);
    http_request_set_method(&req, HTTP_GET);
    http_request_set_url(&req, "https://httpbin.org/get");

    HttpResponse resp;
    http_response_init(&resp);

    bool success = http_client_execute(client, &req, &resp);

    if (success) {
        TEST_ASSERT(resp.status_code == 200, "HTTP GET returns 200");
        TEST_ASSERT(resp.body != NULL, "Response has body");
        TEST_ASSERT(resp.body_len > 0, "Response body has length");
        TEST_ASSERT(strstr(resp.body, "httpbin.org") != NULL, "Response contains expected content");
    } else {
        // Network might not be available, that's okay for unit tests
        printf("  SKIP: Network request failed (may be offline): %s\n",
               resp.error ? resp.error : "unknown error");
        inc_tests_run();
    }

    http_response_cleanup(&resp);
    http_request_cleanup(&req);
    http_client_destroy(client);
}

void test_http_client(void)
{
    test_client_create_destroy();
    test_client_timeout();
    test_request_init();
    test_request_method();
    test_request_url();
    test_request_headers();
    test_request_body();
    test_response_init();
    test_response_cleanup();
    test_real_http_request();
}

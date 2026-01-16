#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#define HTTP_MAX_URL_LEN 2048
#define HTTP_MAX_HEADERS 32
#define HTTP_MAX_HEADER_LEN 512
#define HTTP_TIMEOUT_CONNECT 30L
#define HTTP_TIMEOUT_TRANSFER 120L

typedef enum HttpMethod {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE
} HttpMethod;

typedef struct HttpRequest {
    HttpMethod method;
    char url[HTTP_MAX_URL_LEN];
    char headers[HTTP_MAX_HEADERS][HTTP_MAX_HEADER_LEN];
    int header_count;
    char *body;
    size_t body_len;
} HttpRequest;

typedef struct HttpResponse {
    int status_code;
    char *body;
    size_t body_len;
    char *error;
} HttpResponse;

typedef struct HttpClient {
    bool initialized;
    long timeout_connect;
    long timeout_transfer;
} HttpClient;

// Create and destroy HTTP client
HttpClient *http_client_create(void);
void http_client_destroy(HttpClient *client);

// Configure client
void http_client_set_timeout(HttpClient *client, long connect_timeout, long transfer_timeout);

// Request functions
void http_request_init(HttpRequest *req);
void http_request_cleanup(HttpRequest *req);
void http_request_set_method(HttpRequest *req, HttpMethod method);
void http_request_set_url(HttpRequest *req, const char *url);
void http_request_add_header(HttpRequest *req, const char *name, const char *value);
void http_request_set_body(HttpRequest *req, const char *body, size_t len);
void http_request_set_body_string(HttpRequest *req, const char *body);

// Response functions
void http_response_init(HttpResponse *resp);
void http_response_cleanup(HttpResponse *resp);

// Execute request
bool http_client_execute(HttpClient *client, const HttpRequest *req, HttpResponse *resp);

// Utility
const char *http_method_to_string(HttpMethod method);

#endif // HTTP_CLIENT_H

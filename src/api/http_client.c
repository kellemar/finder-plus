#include "http_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>

typedef struct ResponseBuffer {
    char *data;
    size_t size;
    size_t capacity;
} ResponseBuffer;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t real_size = size * nmemb;
    ResponseBuffer *buf = (ResponseBuffer *)userp;

    if (buf->size + real_size + 1 > buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        if (new_capacity < buf->size + real_size + 1) {
            new_capacity = buf->size + real_size + 1;
        }
        char *new_data = (char *)realloc(buf->data, new_capacity);
        if (!new_data) {
            return 0;
        }
        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->size, contents, real_size);
    buf->size += real_size;
    buf->data[buf->size] = '\0';

    return real_size;
}

HttpClient *http_client_create(void)
{
    HttpClient *client = (HttpClient *)calloc(1, sizeof(HttpClient));
    if (!client) return NULL;

    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        free(client);
        return NULL;
    }

    client->initialized = true;
    client->timeout_connect = HTTP_TIMEOUT_CONNECT;
    client->timeout_transfer = HTTP_TIMEOUT_TRANSFER;

    return client;
}

void http_client_destroy(HttpClient *client)
{
    if (client) {
        if (client->initialized) {
            curl_global_cleanup();
        }
        free(client);
    }
}

void http_client_set_timeout(HttpClient *client, long connect_timeout, long transfer_timeout)
{
    if (!client) return;
    client->timeout_connect = connect_timeout > 0 ? connect_timeout : HTTP_TIMEOUT_CONNECT;
    client->timeout_transfer = transfer_timeout > 0 ? transfer_timeout : HTTP_TIMEOUT_TRANSFER;
}

void http_request_init(HttpRequest *req)
{
    if (!req) return;
    memset(req, 0, sizeof(HttpRequest));
    req->method = HTTP_GET;
}

void http_request_cleanup(HttpRequest *req)
{
    if (!req) return;
    if (req->body) {
        free(req->body);
        req->body = NULL;
    }
    req->body_len = 0;
}

void http_request_set_method(HttpRequest *req, HttpMethod method)
{
    if (!req) return;
    req->method = method;
}

void http_request_set_url(HttpRequest *req, const char *url)
{
    if (!req || !url) return;
    strncpy(req->url, url, HTTP_MAX_URL_LEN - 1);
    req->url[HTTP_MAX_URL_LEN - 1] = '\0';
}

void http_request_add_header(HttpRequest *req, const char *name, const char *value)
{
    if (!req || !name || !value) return;
    if (req->header_count >= HTTP_MAX_HEADERS) return;

    snprintf(req->headers[req->header_count], HTTP_MAX_HEADER_LEN, "%s: %s", name, value);
    req->header_count++;
}

void http_request_set_body(HttpRequest *req, const char *body, size_t len)
{
    if (!req) return;

    if (req->body) {
        free(req->body);
        req->body = NULL;
    }

    if (body && len > 0) {
        req->body = (char *)malloc(len + 1);
        if (req->body) {
            memcpy(req->body, body, len);
            req->body[len] = '\0';
            req->body_len = len;
        }
    }
}

void http_request_set_body_string(HttpRequest *req, const char *body)
{
    if (!req || !body) return;
    http_request_set_body(req, body, strlen(body));
}

void http_response_init(HttpResponse *resp)
{
    if (!resp) return;
    memset(resp, 0, sizeof(HttpResponse));
}

void http_response_cleanup(HttpResponse *resp)
{
    if (!resp) return;
    if (resp->body) {
        free(resp->body);
        resp->body = NULL;
    }
    if (resp->error) {
        free(resp->error);
        resp->error = NULL;
    }
    resp->status_code = 0;
    resp->body_len = 0;
}

bool http_client_execute(HttpClient *client, const HttpRequest *req, HttpResponse *resp)
{
    if (!client || !req || !resp) return false;
    if (!client->initialized) {
        resp->error = strdup("HTTP client not initialized");
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        resp->error = strdup("Failed to initialize curl");
        return false;
    }

    ResponseBuffer buffer = {0};
    buffer.capacity = 4096;
    buffer.data = (char *)malloc(buffer.capacity);
    if (!buffer.data) {
        curl_easy_cleanup(curl);
        resp->error = strdup("Failed to allocate response buffer");
        return false;
    }
    buffer.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, client->timeout_transfer);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, client->timeout_connect);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

    // Set HTTP method
    switch (req->method) {
        case HTTP_GET:
            break;
        case HTTP_POST:
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if (req->body && req->body_len > 0) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
            }
            break;
        case HTTP_PUT:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            if (req->body && req->body_len > 0) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
            }
            break;
        case HTTP_DELETE:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            break;
    }

    // Set headers
    struct curl_slist *headers = NULL;
    for (int i = 0; i < req->header_count; i++) {
        headers = curl_slist_append(headers, req->headers[i]);
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    // Execute request
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        resp->error = strdup(curl_easy_strerror(res));
        free(buffer.data);
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    // Get response info
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    resp->status_code = (int)http_code;
    resp->body = buffer.data;
    resp->body_len = buffer.size;

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return true;
}

const char *http_method_to_string(HttpMethod method)
{
    switch (method) {
        case HTTP_GET: return "GET";
        case HTTP_POST: return "POST";
        case HTTP_PUT: return "PUT";
        case HTTP_DELETE: return "DELETE";
        default: return "UNKNOWN";
    }
}

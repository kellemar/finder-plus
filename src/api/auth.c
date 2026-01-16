#include "auth.h"
#include "claude_client.h"
#include "../../external/cJSON/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Helper to read and parse JSON config file
static cJSON *auth_read_config_file(const char *config_path)
{
    if (!config_path) return NULL;

    FILE *file = fopen(config_path, "r");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 1024 * 1024) {
        fclose(file);
        return NULL;
    }

    char *buffer = (char *)malloc((size_t)file_size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    size_t read_size = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);
    buffer[read_size] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    return root;
}

void auth_init(AuthState *auth)
{
    if (!auth) return;
    memset(auth, 0, sizeof(AuthState));
    auth->status = AUTH_STATUS_UNKNOWN;
    auth->source = AUTH_SOURCE_NONE;
    auth->gemini_status = AUTH_STATUS_UNKNOWN;
    auth->gemini_source = AUTH_SOURCE_NONE;
}

bool auth_load_from_env(AuthState *auth)
{
    if (!auth) return false;

    const char *api_key = getenv("CLAUDE_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        return false;
    }

    strncpy(auth->api_key, api_key, AUTH_API_KEY_MAX_LEN - 1);
    auth->api_key[AUTH_API_KEY_MAX_LEN - 1] = '\0';
    auth->source = AUTH_SOURCE_ENV;
    auth->status = AUTH_STATUS_VALID;

    return true;
}

bool auth_load_from_config(AuthState *auth, const char *config_path)
{
    if (!auth) return false;

    cJSON *root = auth_read_config_file(config_path);
    if (!root) return false;

    cJSON *api_key = cJSON_GetObjectItem(root, "api_key");
    if (!api_key) {
        api_key = cJSON_GetObjectItem(root, "claude_api_key");
    }

    bool found = false;
    if (api_key && cJSON_IsString(api_key) && strlen(api_key->valuestring) > 0) {
        strncpy(auth->api_key, api_key->valuestring, AUTH_API_KEY_MAX_LEN - 1);
        auth->api_key[AUTH_API_KEY_MAX_LEN - 1] = '\0';
        auth->source = AUTH_SOURCE_CONFIG;
        auth->status = AUTH_STATUS_VALID;
        found = true;
    }

    cJSON_Delete(root);
    return found;
}

bool auth_load(AuthState *auth, const char *config_path)
{
    if (!auth) return false;

    // Try environment variable first
    if (auth_load_from_env(auth)) {
        return true;
    }

    // Try config file
    if (config_path && auth_load_from_config(auth, config_path)) {
        return true;
    }

    // Try default config paths
    const char *home = getenv("HOME");
    if (home) {
        char default_config[512];

        // Try ~/.config/finder-plus/config.json
        snprintf(default_config, sizeof(default_config), "%s/.config/finder-plus/config.json", home);
        if (auth_load_from_config(auth, default_config)) {
            return true;
        }

        // Try ~/.finder-plus.json
        snprintf(default_config, sizeof(default_config), "%s/.finder-plus.json", home);
        if (auth_load_from_config(auth, default_config)) {
            return true;
        }
    }

    auth->status = AUTH_STATUS_MISSING;
    return false;
}

bool auth_validate(AuthState *auth)
{
    if (!auth || strlen(auth->api_key) == 0) {
        if (auth) auth->status = AUTH_STATUS_MISSING;
        return false;
    }

    // Create a minimal test request
    ClaudeClient *client = claude_client_create(auth->api_key);
    if (!client) {
        auth->status = AUTH_STATUS_INVALID;
        return false;
    }

    ClaudeMessageRequest req;
    claude_request_init(&req);
    claude_request_set_max_tokens(&req, 10);
    claude_request_add_user_message(&req, "Hi");

    ClaudeMessageResponse resp;
    claude_response_init(&resp);

    bool success = claude_send_message(client, &req, &resp);

    if (success && resp.stop_reason != CLAUDE_STOP_ERROR) {
        auth->status = AUTH_STATUS_VALID;
        auth->validated = true;
    } else {
        if (resp.error && strstr(resp.error, "401")) {
            auth->status = AUTH_STATUS_INVALID;
        } else if (resp.error && strstr(resp.error, "expired")) {
            auth->status = AUTH_STATUS_EXPIRED;
        } else {
            auth->status = AUTH_STATUS_INVALID;
        }
    }

    claude_response_cleanup(&resp);
    claude_request_cleanup(&req);
    claude_client_destroy(client);

    return auth->status == AUTH_STATUS_VALID;
}

const char *auth_status_to_string(AuthStatus status)
{
    switch (status) {
        case AUTH_STATUS_UNKNOWN: return "Unknown";
        case AUTH_STATUS_VALID: return "Valid";
        case AUTH_STATUS_INVALID: return "Invalid";
        case AUTH_STATUS_MISSING: return "Missing";
        case AUTH_STATUS_EXPIRED: return "Expired";
        default: return "Unknown";
    }
}

const char *auth_source_to_string(AuthSource source)
{
    switch (source) {
        case AUTH_SOURCE_NONE: return "None";
        case AUTH_SOURCE_ENV: return "Environment Variable";
        case AUTH_SOURCE_CONFIG: return "Config File";
        case AUTH_SOURCE_KEYCHAIN: return "Keychain";
        default: return "Unknown";
    }
}

bool auth_is_ready(const AuthState *auth)
{
    return auth && strlen(auth->api_key) > 0 &&
           (auth->status == AUTH_STATUS_VALID || auth->status == AUTH_STATUS_UNKNOWN);
}

void auth_clear(AuthState *auth)
{
    if (auth) {
        memset(auth->api_key, 0, AUTH_API_KEY_MAX_LEN);
        auth->status = AUTH_STATUS_UNKNOWN;
        auth->source = AUTH_SOURCE_NONE;
        auth->validated = false;
        memset(auth->gemini_api_key, 0, AUTH_API_KEY_MAX_LEN);
        auth->gemini_status = AUTH_STATUS_UNKNOWN;
        auth->gemini_source = AUTH_SOURCE_NONE;
        auth->gemini_validated = false;
    }
}

bool auth_load_gemini_from_env(AuthState *auth)
{
    if (!auth) return false;

    const char *api_key = getenv("GEMINI_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        return false;
    }

    strncpy(auth->gemini_api_key, api_key, AUTH_API_KEY_MAX_LEN - 1);
    auth->gemini_api_key[AUTH_API_KEY_MAX_LEN - 1] = '\0';
    auth->gemini_source = AUTH_SOURCE_ENV;
    auth->gemini_status = AUTH_STATUS_VALID;

    return true;
}

static bool auth_load_gemini_from_config(AuthState *auth, const char *config_path)
{
    if (!auth) return false;

    cJSON *root = auth_read_config_file(config_path);
    if (!root) return false;

    cJSON *api_key = cJSON_GetObjectItem(root, "gemini_api_key");

    bool found = false;
    if (api_key && cJSON_IsString(api_key) && strlen(api_key->valuestring) > 0) {
        strncpy(auth->gemini_api_key, api_key->valuestring, AUTH_API_KEY_MAX_LEN - 1);
        auth->gemini_api_key[AUTH_API_KEY_MAX_LEN - 1] = '\0';
        auth->gemini_source = AUTH_SOURCE_CONFIG;
        auth->gemini_status = AUTH_STATUS_VALID;
        found = true;
    }

    cJSON_Delete(root);
    return found;
}

bool auth_load_gemini(AuthState *auth, const char *config_path)
{
    if (!auth) return false;

    // Try environment variable first
    if (auth_load_gemini_from_env(auth)) {
        return true;
    }

    // Try config file
    if (config_path && auth_load_gemini_from_config(auth, config_path)) {
        return true;
    }

    // Try default config paths
    const char *home = getenv("HOME");
    if (home) {
        char default_config[512];

        // Try ~/.config/finder-plus/config.json
        snprintf(default_config, sizeof(default_config), "%s/.config/finder-plus/config.json", home);
        if (auth_load_gemini_from_config(auth, default_config)) {
            return true;
        }

        // Try ~/.finder-plus.json
        snprintf(default_config, sizeof(default_config), "%s/.finder-plus.json", home);
        if (auth_load_gemini_from_config(auth, default_config)) {
            return true;
        }
    }

    auth->gemini_status = AUTH_STATUS_MISSING;
    return false;
}

bool auth_gemini_is_ready(const AuthState *auth)
{
    return auth && strlen(auth->gemini_api_key) > 0 &&
           (auth->gemini_status == AUTH_STATUS_VALID || auth->gemini_status == AUTH_STATUS_UNKNOWN);
}

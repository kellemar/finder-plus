#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>

#define AUTH_API_KEY_MAX_LEN 256

// Authentication status
typedef enum AuthStatus {
    AUTH_STATUS_UNKNOWN = 0,
    AUTH_STATUS_VALID,
    AUTH_STATUS_INVALID,
    AUTH_STATUS_MISSING,
    AUTH_STATUS_EXPIRED
} AuthStatus;

// Authentication source
typedef enum AuthSource {
    AUTH_SOURCE_NONE = 0,
    AUTH_SOURCE_ENV,
    AUTH_SOURCE_CONFIG,
    AUTH_SOURCE_KEYCHAIN
} AuthSource;

// Authentication state
typedef struct AuthState {
    char api_key[AUTH_API_KEY_MAX_LEN];
    AuthStatus status;
    AuthSource source;
    bool validated;
    // Gemini API key (for image generation)
    char gemini_api_key[AUTH_API_KEY_MAX_LEN];
    AuthStatus gemini_status;
    AuthSource gemini_source;
    bool gemini_validated;
} AuthState;

// Initialize auth state
void auth_init(AuthState *auth);

// Load API key from various sources (priority: env > config > keychain)
bool auth_load(AuthState *auth, const char *config_path);

// Load from specific source
bool auth_load_from_env(AuthState *auth);
bool auth_load_from_config(AuthState *auth, const char *config_path);

// Validate API key by making a test request
bool auth_validate(AuthState *auth);

// Get human-readable status
const char *auth_status_to_string(AuthStatus status);
const char *auth_source_to_string(AuthSource source);

// Check if we have a usable API key
bool auth_is_ready(const AuthState *auth);

// Clear sensitive data
void auth_clear(AuthState *auth);

// Gemini API key functions
bool auth_load_gemini(AuthState *auth, const char *config_path);
bool auth_load_gemini_from_env(AuthState *auth);
bool auth_gemini_is_ready(const AuthState *auth);

#endif // AUTH_H

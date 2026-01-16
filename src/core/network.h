#ifndef NETWORK_H
#define NETWORK_H

#include "filesystem.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_CONNECTIONS 16
#define MAX_SAVED_PROFILES 32
#define NETWORK_HOST_MAX 256
#define NETWORK_USER_MAX 128
#define NETWORK_PASS_MAX 256
#define NETWORK_PATH_MAX 512

// Connection types
typedef enum {
    CONN_TYPE_NONE = 0,
    CONN_TYPE_SFTP,
    CONN_TYPE_SMB
} ConnectionType;

// Connection status
typedef enum {
    CONN_STATUS_DISCONNECTED,
    CONN_STATUS_CONNECTING,
    CONN_STATUS_CONNECTED,
    CONN_STATUS_ERROR,
    CONN_STATUS_RECONNECTING
} ConnectionStatus;

// Authentication methods
typedef enum {
    AUTH_NONE,
    AUTH_PASSWORD,
    AUTH_PUBLICKEY,
    AUTH_KEYBOARD_INTERACTIVE
} AuthMethod;

// Saved connection profile
typedef struct ConnectionProfile {
    char name[64];                          // Display name
    ConnectionType type;
    char host[NETWORK_HOST_MAX];
    int port;                               // 0 = default (22 for SFTP, 445 for SMB)
    char username[NETWORK_USER_MAX];
    char password[NETWORK_PASS_MAX];        // Optional - can prompt
    char private_key_path[PATH_MAX_LEN];    // For SFTP public key auth
    char remote_path[NETWORK_PATH_MAX];     // Initial path
    bool save_password;                     // Whether to save password
    bool auto_connect;                      // Connect on startup
} ConnectionProfile;

// Active connection
typedef struct NetworkConnection {
    int id;                                 // Unique connection ID
    ConnectionProfile profile;
    ConnectionStatus status;
    char error_message[256];
    char current_path[NETWORK_PATH_MAX];    // Current remote path

    // SFTP-specific handles (opaque pointers)
    void *ssh_session;                      // LIBSSH2_SESSION*
    void *sftp_session;                     // LIBSSH2_SFTP*
    int socket;

    // Reconnect state
    int reconnect_attempts;
    double last_activity;
} NetworkConnection;

// Network manager state
typedef struct NetworkManager {
    NetworkConnection connections[MAX_CONNECTIONS];
    int connection_count;
    int active_connection;                  // Index of currently active connection (-1 = local)

    ConnectionProfile saved_profiles[MAX_SAVED_PROFILES];
    int profile_count;

    bool initialized;
} NetworkManager;

// Initialize/shutdown network subsystem
bool network_init(NetworkManager *mgr);
void network_shutdown(NetworkManager *mgr);

// Connection profile management
bool network_load_profiles(NetworkManager *mgr);
bool network_save_profiles(NetworkManager *mgr);
int network_add_profile(NetworkManager *mgr, const ConnectionProfile *profile);
bool network_remove_profile(NetworkManager *mgr, int index);
bool network_update_profile(NetworkManager *mgr, int index, const ConnectionProfile *profile);
ConnectionProfile* network_get_profile(NetworkManager *mgr, int index);

// Connection management
int network_connect(NetworkManager *mgr, const ConnectionProfile *profile);
bool network_disconnect(NetworkManager *mgr, int conn_id);
bool network_reconnect(NetworkManager *mgr, int conn_id);
NetworkConnection* network_get_connection(NetworkManager *mgr, int conn_id);
ConnectionStatus network_get_status(NetworkManager *mgr, int conn_id);
const char* network_get_error(NetworkManager *mgr, int conn_id);

// Set active connection for directory browsing
void network_set_active(NetworkManager *mgr, int conn_id);
int network_get_active(NetworkManager *mgr);
bool network_is_remote_active(NetworkManager *mgr);

// Remote directory operations
bool network_read_directory(NetworkManager *mgr, int conn_id, const char *path, DirectoryState *dir);
bool network_change_directory(NetworkManager *mgr, int conn_id, const char *path);
bool network_make_directory(NetworkManager *mgr, int conn_id, const char *path);
bool network_remove_directory(NetworkManager *mgr, int conn_id, const char *path);

// Remote file operations
bool network_download_file(NetworkManager *mgr, int conn_id, const char *remote_path, const char *local_path);
bool network_upload_file(NetworkManager *mgr, int conn_id, const char *local_path, const char *remote_path);
bool network_delete_file(NetworkManager *mgr, int conn_id, const char *path);
bool network_rename_file(NetworkManager *mgr, int conn_id, const char *old_path, const char *new_path);

// Utility functions
const char* network_connection_type_name(ConnectionType type);
const char* network_status_name(ConnectionStatus status);
bool network_validate_profile(const ConnectionProfile *profile, char *error, size_t error_size);

// SFTP-specific functions
bool sftp_connect(NetworkConnection *conn);
void sftp_disconnect(NetworkConnection *conn);
bool sftp_read_directory(NetworkConnection *conn, const char *path, DirectoryState *dir);
bool sftp_download(NetworkConnection *conn, const char *remote, const char *local);
bool sftp_upload(NetworkConnection *conn, const char *local, const char *remote);
bool sftp_mkdir(NetworkConnection *conn, const char *path);
bool sftp_rmdir(NetworkConnection *conn, const char *path);
bool sftp_unlink(NetworkConnection *conn, const char *path);
bool sftp_rename(NetworkConnection *conn, const char *old_path, const char *new_path);

// SMB-specific functions (stub for now)
bool smb_connect(NetworkConnection *conn);
void smb_disconnect(NetworkConnection *conn);
bool smb_read_directory(NetworkConnection *conn, const char *path, DirectoryState *dir);

#endif // NETWORK_H

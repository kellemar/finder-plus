#include "network.h"
#include "../utils/config.h"
#include "../../external/cJSON/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>

#include <libssh2.h>
#include <libssh2_sftp.h>

// Default ports
#define SFTP_DEFAULT_PORT 22
#define SMB_DEFAULT_PORT 445

// Reconnection settings
#define MAX_RECONNECT_ATTEMPTS 3
#define RECONNECT_DELAY_MS 1000

// Config file path for saved profiles
static const char* get_profiles_path(void)
{
    static char path[PATH_MAX_LEN];
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/.config/finder-plus/network_profiles.json", home);
    } else {
        snprintf(path, sizeof(path), "/tmp/finder_plus_network_profiles.json");
    }
    return path;
}

// Create a TCP socket and connect to host:port
static int connect_to_host(const char *hostname, int port)
{
    struct addrinfo hints, *res, *p;
    int sockfd = -1;
    char port_str[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_str, sizeof(port_str), "%d", port);

    int status = getaddrinfo(hostname, port_str, &hints, &res);
    if (status != 0) {
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) continue;

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(res);
    return sockfd;
}

bool network_init(NetworkManager *mgr)
{
    memset(mgr, 0, sizeof(NetworkManager));
    mgr->active_connection = -1;

    // Initialize libssh2
    int rc = libssh2_init(0);
    if (rc != 0) {
        return false;
    }

    mgr->initialized = true;

    // Load saved profiles
    network_load_profiles(mgr);

    return true;
}

void network_shutdown(NetworkManager *mgr)
{
    // Disconnect all connections
    for (int i = 0; i < mgr->connection_count; i++) {
        if (mgr->connections[i].status == CONN_STATUS_CONNECTED) {
            network_disconnect(mgr, mgr->connections[i].id);
        }
    }

    // Save profiles
    network_save_profiles(mgr);

    // Shutdown libssh2
    if (mgr->initialized) {
        libssh2_exit();
        mgr->initialized = false;
    }
}

bool network_load_profiles(NetworkManager *mgr)
{
    const char *path = get_profiles_path();
    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }

    // Read file content
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {
        fclose(f);
        return false;
    }

    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return false;
    }

    size_t read_size = fread(content, 1, size, f);
    fclose(f);

    if (read_size != (size_t)size) {
        free(content);
        return false;
    }
    content[size] = '\0';

    // Parse JSON using cJSON
    cJSON *root = cJSON_Parse(content);
    free(content);

    if (!root) {
        return false;
    }

    cJSON *profiles = cJSON_GetObjectItem(root, "profiles");
    if (!cJSON_IsArray(profiles)) {
        cJSON_Delete(root);
        return false;
    }

    int profile_count = cJSON_GetArraySize(profiles);
    if (profile_count > MAX_SAVED_PROFILES) {
        profile_count = MAX_SAVED_PROFILES;
    }

    for (int i = 0; i < profile_count; i++) {
        cJSON *item = cJSON_GetArrayItem(profiles, i);
        if (!cJSON_IsObject(item)) continue;

        ConnectionProfile *p = &mgr->saved_profiles[mgr->profile_count];
        memset(p, 0, sizeof(ConnectionProfile));

        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *type = cJSON_GetObjectItem(item, "type");
        cJSON *host = cJSON_GetObjectItem(item, "host");
        cJSON *port = cJSON_GetObjectItem(item, "port");
        cJSON *username = cJSON_GetObjectItem(item, "username");
        cJSON *password = cJSON_GetObjectItem(item, "password");
        cJSON *key_path = cJSON_GetObjectItem(item, "private_key_path");
        cJSON *remote_path = cJSON_GetObjectItem(item, "remote_path");
        cJSON *save_pass = cJSON_GetObjectItem(item, "save_password");
        cJSON *auto_conn = cJSON_GetObjectItem(item, "auto_connect");

        if (cJSON_IsString(name)) {
            strncpy(p->name, name->valuestring, sizeof(p->name) - 1);
        }
        if (cJSON_IsNumber(type)) {
            p->type = (ConnectionType)type->valueint;
        }
        if (cJSON_IsString(host)) {
            strncpy(p->host, host->valuestring, sizeof(p->host) - 1);
        }
        if (cJSON_IsNumber(port)) {
            p->port = port->valueint;
        }
        if (cJSON_IsString(username)) {
            strncpy(p->username, username->valuestring, sizeof(p->username) - 1);
        }
        if (cJSON_IsString(password)) {
            strncpy(p->password, password->valuestring, sizeof(p->password) - 1);
        }
        if (cJSON_IsString(key_path)) {
            strncpy(p->private_key_path, key_path->valuestring, sizeof(p->private_key_path) - 1);
        }
        if (cJSON_IsString(remote_path)) {
            strncpy(p->remote_path, remote_path->valuestring, sizeof(p->remote_path) - 1);
        }
        if (cJSON_IsBool(save_pass)) {
            p->save_password = cJSON_IsTrue(save_pass);
        }
        if (cJSON_IsBool(auto_conn)) {
            p->auto_connect = cJSON_IsTrue(auto_conn);
        }

        mgr->profile_count++;
    }

    cJSON_Delete(root);
    return true;
}

bool network_save_profiles(NetworkManager *mgr)
{
    const char *path = get_profiles_path();

    // Create directory if needed
    char dir[PATH_MAX_LEN];
    strncpy(dir, path, sizeof(dir) - 1);
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir, 0755);
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        return false;
    }

    // Write JSON format profiles
    fprintf(f, "{\n  \"profiles\": [\n");

    for (int i = 0; i < mgr->profile_count; i++) {
        ConnectionProfile *p = &mgr->saved_profiles[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", p->name);
        fprintf(f, "      \"type\": %d,\n", p->type);
        fprintf(f, "      \"host\": \"%s\",\n", p->host);
        fprintf(f, "      \"port\": %d,\n", p->port);
        fprintf(f, "      \"username\": \"%s\",\n", p->username);
        if (p->save_password) {
            fprintf(f, "      \"password\": \"%s\",\n", p->password);
        }
        fprintf(f, "      \"remote_path\": \"%s\",\n", p->remote_path);
        fprintf(f, "      \"auto_connect\": %s\n", p->auto_connect ? "true" : "false");
        fprintf(f, "    }%s\n", (i < mgr->profile_count - 1) ? "," : "");
    }

    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}

int network_add_profile(NetworkManager *mgr, const ConnectionProfile *profile)
{
    if (mgr->profile_count >= MAX_SAVED_PROFILES) {
        return -1;
    }

    int index = mgr->profile_count;
    mgr->saved_profiles[index] = *profile;
    mgr->profile_count++;

    network_save_profiles(mgr);
    return index;
}

bool network_remove_profile(NetworkManager *mgr, int index)
{
    if (index < 0 || index >= mgr->profile_count) {
        return false;
    }

    // Shift remaining profiles
    for (int i = index; i < mgr->profile_count - 1; i++) {
        mgr->saved_profiles[i] = mgr->saved_profiles[i + 1];
    }
    mgr->profile_count--;

    network_save_profiles(mgr);
    return true;
}

bool network_update_profile(NetworkManager *mgr, int index, const ConnectionProfile *profile)
{
    if (index < 0 || index >= mgr->profile_count) {
        return false;
    }

    mgr->saved_profiles[index] = *profile;
    network_save_profiles(mgr);
    return true;
}

ConnectionProfile* network_get_profile(NetworkManager *mgr, int index)
{
    if (index < 0 || index >= mgr->profile_count) {
        return NULL;
    }
    return &mgr->saved_profiles[index];
}

// Generate unique connection ID
static int next_connection_id = 1;

int network_connect(NetworkManager *mgr, const ConnectionProfile *profile)
{
    if (mgr->connection_count >= MAX_CONNECTIONS) {
        return -1;
    }

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (mgr->connections[i].id == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        return -1;
    }

    NetworkConnection *conn = &mgr->connections[slot];
    memset(conn, 0, sizeof(NetworkConnection));

    conn->id = next_connection_id++;
    conn->profile = *profile;
    conn->status = CONN_STATUS_CONNECTING;
    conn->socket = -1;

    // Set default port if not specified
    if (conn->profile.port == 0) {
        switch (conn->profile.type) {
            case CONN_TYPE_SFTP: conn->profile.port = SFTP_DEFAULT_PORT; break;
            case CONN_TYPE_SMB: conn->profile.port = SMB_DEFAULT_PORT; break;
            default: break;
        }
    }

    // Copy initial path
    if (profile->remote_path[0] != '\0') {
        strncpy(conn->current_path, profile->remote_path, NETWORK_PATH_MAX - 1);
    } else {
        strcpy(conn->current_path, "/");
    }

    mgr->connection_count++;

    // Perform actual connection based on type
    bool success = false;
    switch (profile->type) {
        case CONN_TYPE_SFTP:
            success = sftp_connect(conn);
            break;
        case CONN_TYPE_SMB:
            success = smb_connect(conn);
            break;
        default:
            strncpy(conn->error_message, "Unknown connection type", sizeof(conn->error_message) - 1);
            break;
    }

    if (success) {
        conn->status = CONN_STATUS_CONNECTED;
        conn->last_activity = (double)time(NULL);
    } else {
        conn->status = CONN_STATUS_ERROR;
    }

    return conn->id;
}

bool network_disconnect(NetworkManager *mgr, int conn_id)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn) {
        return false;
    }

    switch (conn->profile.type) {
        case CONN_TYPE_SFTP:
            sftp_disconnect(conn);
            break;
        case CONN_TYPE_SMB:
            smb_disconnect(conn);
            break;
        default:
            break;
    }

    conn->status = CONN_STATUS_DISCONNECTED;

    // Clear the slot
    conn->id = 0;
    mgr->connection_count--;

    if (mgr->active_connection == conn_id) {
        mgr->active_connection = -1;
    }

    return true;
}

bool network_reconnect(NetworkManager *mgr, int conn_id)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn) {
        return false;
    }

    if (conn->reconnect_attempts >= MAX_RECONNECT_ATTEMPTS) {
        conn->status = CONN_STATUS_ERROR;
        strncpy(conn->error_message, "Max reconnect attempts reached", sizeof(conn->error_message) - 1);
        return false;
    }

    conn->status = CONN_STATUS_RECONNECTING;
    conn->reconnect_attempts++;

    // Disconnect first
    switch (conn->profile.type) {
        case CONN_TYPE_SFTP:
            sftp_disconnect(conn);
            break;
        case CONN_TYPE_SMB:
            smb_disconnect(conn);
            break;
        default:
            break;
    }

    // Reconnect
    bool success = false;
    switch (conn->profile.type) {
        case CONN_TYPE_SFTP:
            success = sftp_connect(conn);
            break;
        case CONN_TYPE_SMB:
            success = smb_connect(conn);
            break;
        default:
            break;
    }

    if (success) {
        conn->status = CONN_STATUS_CONNECTED;
        conn->reconnect_attempts = 0;
        conn->last_activity = (double)time(NULL);
    } else {
        conn->status = CONN_STATUS_ERROR;
    }

    return success;
}

NetworkConnection* network_get_connection(NetworkManager *mgr, int conn_id)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (mgr->connections[i].id == conn_id) {
            return &mgr->connections[i];
        }
    }
    return NULL;
}

ConnectionStatus network_get_status(NetworkManager *mgr, int conn_id)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn) {
        return CONN_STATUS_DISCONNECTED;
    }
    return conn->status;
}

const char* network_get_error(NetworkManager *mgr, int conn_id)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn) {
        return "Connection not found";
    }
    return conn->error_message;
}

void network_set_active(NetworkManager *mgr, int conn_id)
{
    mgr->active_connection = conn_id;
}

int network_get_active(NetworkManager *mgr)
{
    return mgr->active_connection;
}

bool network_is_remote_active(NetworkManager *mgr)
{
    return mgr->active_connection >= 0;
}

bool network_read_directory(NetworkManager *mgr, int conn_id, const char *path, DirectoryState *dir)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn || conn->status != CONN_STATUS_CONNECTED) {
        return false;
    }

    bool success = false;
    switch (conn->profile.type) {
        case CONN_TYPE_SFTP:
            success = sftp_read_directory(conn, path, dir);
            break;
        case CONN_TYPE_SMB:
            success = smb_read_directory(conn, path, dir);
            break;
        default:
            break;
    }

    if (success) {
        strncpy(conn->current_path, path, NETWORK_PATH_MAX - 1);
        conn->last_activity = (double)time(NULL);
    }

    return success;
}

bool network_change_directory(NetworkManager *mgr, int conn_id, const char *path)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn || conn->status != CONN_STATUS_CONNECTED) {
        return false;
    }

    strncpy(conn->current_path, path, NETWORK_PATH_MAX - 1);
    conn->last_activity = (double)time(NULL);
    return true;
}

bool network_make_directory(NetworkManager *mgr, int conn_id, const char *path)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn || conn->status != CONN_STATUS_CONNECTED) {
        return false;
    }

    bool success = false;
    switch (conn->profile.type) {
        case CONN_TYPE_SFTP:
            success = sftp_mkdir(conn, path);
            break;
        default:
            break;
    }

    if (success) {
        conn->last_activity = (double)time(NULL);
    }

    return success;
}

bool network_remove_directory(NetworkManager *mgr, int conn_id, const char *path)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn || conn->status != CONN_STATUS_CONNECTED) {
        return false;
    }

    bool success = false;
    switch (conn->profile.type) {
        case CONN_TYPE_SFTP:
            success = sftp_rmdir(conn, path);
            break;
        default:
            break;
    }

    if (success) {
        conn->last_activity = (double)time(NULL);
    }

    return success;
}

bool network_download_file(NetworkManager *mgr, int conn_id, const char *remote_path, const char *local_path)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn || conn->status != CONN_STATUS_CONNECTED) {
        return false;
    }

    bool success = false;
    switch (conn->profile.type) {
        case CONN_TYPE_SFTP:
            success = sftp_download(conn, remote_path, local_path);
            break;
        default:
            break;
    }

    if (success) {
        conn->last_activity = (double)time(NULL);
    }

    return success;
}

bool network_upload_file(NetworkManager *mgr, int conn_id, const char *local_path, const char *remote_path)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn || conn->status != CONN_STATUS_CONNECTED) {
        return false;
    }

    bool success = false;
    switch (conn->profile.type) {
        case CONN_TYPE_SFTP:
            success = sftp_upload(conn, local_path, remote_path);
            break;
        default:
            break;
    }

    if (success) {
        conn->last_activity = (double)time(NULL);
    }

    return success;
}

bool network_delete_file(NetworkManager *mgr, int conn_id, const char *path)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn || conn->status != CONN_STATUS_CONNECTED) {
        return false;
    }

    bool success = false;
    switch (conn->profile.type) {
        case CONN_TYPE_SFTP:
            success = sftp_unlink(conn, path);
            break;
        default:
            break;
    }

    if (success) {
        conn->last_activity = (double)time(NULL);
    }

    return success;
}

bool network_rename_file(NetworkManager *mgr, int conn_id, const char *old_path, const char *new_path)
{
    NetworkConnection *conn = network_get_connection(mgr, conn_id);
    if (!conn || conn->status != CONN_STATUS_CONNECTED) {
        return false;
    }

    bool success = false;
    switch (conn->profile.type) {
        case CONN_TYPE_SFTP:
            success = sftp_rename(conn, old_path, new_path);
            break;
        default:
            break;
    }

    if (success) {
        conn->last_activity = (double)time(NULL);
    }

    return success;
}

const char* network_connection_type_name(ConnectionType type)
{
    switch (type) {
        case CONN_TYPE_NONE: return "None";
        case CONN_TYPE_SFTP: return "SFTP";
        case CONN_TYPE_SMB: return "SMB";
        default: return "Unknown";
    }
}

const char* network_status_name(ConnectionStatus status)
{
    switch (status) {
        case CONN_STATUS_DISCONNECTED: return "Disconnected";
        case CONN_STATUS_CONNECTING: return "Connecting";
        case CONN_STATUS_CONNECTED: return "Connected";
        case CONN_STATUS_ERROR: return "Error";
        case CONN_STATUS_RECONNECTING: return "Reconnecting";
        default: return "Unknown";
    }
}

bool network_validate_profile(const ConnectionProfile *profile, char *error, size_t error_size)
{
    if (profile->type == CONN_TYPE_NONE) {
        snprintf(error, error_size, "Connection type not specified");
        return false;
    }

    if (profile->host[0] == '\0') {
        snprintf(error, error_size, "Host not specified");
        return false;
    }

    if (profile->username[0] == '\0') {
        snprintf(error, error_size, "Username not specified");
        return false;
    }

    return true;
}

// SFTP Implementation

bool sftp_connect(NetworkConnection *conn)
{
    // Connect socket
    int port = conn->profile.port > 0 ? conn->profile.port : SFTP_DEFAULT_PORT;
    conn->socket = connect_to_host(conn->profile.host, port);
    if (conn->socket < 0) {
        snprintf(conn->error_message, sizeof(conn->error_message),
                 "Failed to connect to %s:%d", conn->profile.host, port);
        return false;
    }

    // Create SSH session
    LIBSSH2_SESSION *session = libssh2_session_init();
    if (!session) {
        close(conn->socket);
        conn->socket = -1;
        strncpy(conn->error_message, "Failed to create SSH session", sizeof(conn->error_message) - 1);
        return false;
    }

    // Set session to blocking mode for simplicity
    libssh2_session_set_blocking(session, 1);

    // Perform SSH handshake
    int rc = libssh2_session_handshake(session, conn->socket);
    if (rc) {
        char *errmsg;
        libssh2_session_last_error(session, &errmsg, NULL, 0);
        snprintf(conn->error_message, sizeof(conn->error_message),
                 "SSH handshake failed: %s", errmsg);
        libssh2_session_free(session);
        close(conn->socket);
        conn->socket = -1;
        return false;
    }

    // Authenticate
    bool auth_success = false;

    // Try password authentication if password is provided
    if (conn->profile.password[0] != '\0') {
        rc = libssh2_userauth_password(session, conn->profile.username, conn->profile.password);
        if (rc == 0) {
            auth_success = true;
        }
    }

    // Try public key authentication if key path is provided
    if (!auth_success && conn->profile.private_key_path[0] != '\0') {
        char pub_key_path[PATH_MAX_LEN];
        snprintf(pub_key_path, sizeof(pub_key_path), "%s.pub", conn->profile.private_key_path);

        rc = libssh2_userauth_publickey_fromfile(session,
                                                  conn->profile.username,
                                                  pub_key_path,
                                                  conn->profile.private_key_path,
                                                  conn->profile.password);
        if (rc == 0) {
            auth_success = true;
        }
    }

    // Try agent authentication
    if (!auth_success) {
        LIBSSH2_AGENT *agent = libssh2_agent_init(session);
        if (agent) {
            if (libssh2_agent_connect(agent) == 0) {
                if (libssh2_agent_list_identities(agent) == 0) {
                    struct libssh2_agent_publickey *identity = NULL;
                    while (libssh2_agent_get_identity(agent, &identity, identity) == 0) {
                        if (libssh2_agent_userauth(agent, conn->profile.username, identity) == 0) {
                            auth_success = true;
                            break;
                        }
                    }
                }
                libssh2_agent_disconnect(agent);
            }
            libssh2_agent_free(agent);
        }
    }

    if (!auth_success) {
        strncpy(conn->error_message, "Authentication failed", sizeof(conn->error_message) - 1);
        libssh2_session_disconnect(session, "Auth failed");
        libssh2_session_free(session);
        close(conn->socket);
        conn->socket = -1;
        return false;
    }

    // Initialize SFTP session
    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);
    if (!sftp) {
        char *errmsg;
        libssh2_session_last_error(session, &errmsg, NULL, 0);
        snprintf(conn->error_message, sizeof(conn->error_message),
                 "Failed to init SFTP: %s", errmsg);
        libssh2_session_disconnect(session, "SFTP init failed");
        libssh2_session_free(session);
        close(conn->socket);
        conn->socket = -1;
        return false;
    }

    conn->ssh_session = session;
    conn->sftp_session = sftp;

    return true;
}

void sftp_disconnect(NetworkConnection *conn)
{
    if (conn->sftp_session) {
        libssh2_sftp_shutdown((LIBSSH2_SFTP*)conn->sftp_session);
        conn->sftp_session = NULL;
    }

    if (conn->ssh_session) {
        libssh2_session_disconnect((LIBSSH2_SESSION*)conn->ssh_session, "Disconnecting");
        libssh2_session_free((LIBSSH2_SESSION*)conn->ssh_session);
        conn->ssh_session = NULL;
    }

    if (conn->socket >= 0) {
        close(conn->socket);
        conn->socket = -1;
    }
}

bool sftp_read_directory(NetworkConnection *conn, const char *path, DirectoryState *dir)
{
    if (!conn->sftp_session) {
        return false;
    }

    LIBSSH2_SFTP *sftp = (LIBSSH2_SFTP*)conn->sftp_session;
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_opendir(sftp, path);
    if (!handle) {
        char *errmsg;
        libssh2_session_last_error((LIBSSH2_SESSION*)conn->ssh_session, &errmsg, NULL, 0);
        snprintf(conn->error_message, sizeof(conn->error_message),
                 "Failed to open directory: %s", errmsg);
        return false;
    }

    // Clear existing entries
    directory_state_free(dir);
    directory_state_init(dir);
    strncpy(dir->current_path, path, PATH_MAX_LEN - 1);

    char filename[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    while (1) {
        int rc = libssh2_sftp_readdir(handle, filename, sizeof(filename), &attrs);
        if (rc <= 0) {
            break;
        }

        // Skip . and ..
        if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
            continue;
        }

        // Grow entries array if needed
        if (dir->count >= dir->capacity) {
            int new_capacity = dir->capacity == 0 ? 64 : dir->capacity * 2;
            FileEntry *new_entries = realloc(dir->entries, new_capacity * sizeof(FileEntry));
            if (!new_entries) {
                break;
            }
            dir->entries = new_entries;
            dir->capacity = new_capacity;
        }

        FileEntry *entry = &dir->entries[dir->count];
        memset(entry, 0, sizeof(FileEntry));

        // Set name
        strncpy(entry->name, filename, NAME_MAX_LEN - 1);

        // Set full path
        if (strcmp(path, "/") == 0) {
            snprintf(entry->path, PATH_MAX_LEN, "/%s", filename);
        } else {
            snprintf(entry->path, PATH_MAX_LEN, "%s/%s", path, filename);
        }

        // Set extension
        const char *dot = strrchr(filename, '.');
        if (dot && dot != filename) {
            strncpy(entry->extension, dot + 1, EXTENSION_MAX_LEN - 1);
        }

        // Set type
        entry->is_directory = LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
        entry->is_hidden = (filename[0] == '.');
        entry->is_symlink = LIBSSH2_SFTP_S_ISLNK(attrs.permissions);

        // Set size and times
        entry->size = (off_t)attrs.filesize;
        entry->modified = (time_t)attrs.mtime;
        entry->created = (time_t)attrs.mtime;
        entry->permissions = (mode_t)attrs.permissions;

        dir->count++;
    }

    libssh2_sftp_closedir(handle);

    // Sort entries (directories first, then alphabetical by name)
    directory_sort(dir, SORT_BY_NAME, true);

    return true;
}

bool sftp_download(NetworkConnection *conn, const char *remote, const char *local)
{
    if (!conn->sftp_session) {
        return false;
    }

    LIBSSH2_SFTP *sftp = (LIBSSH2_SFTP*)conn->sftp_session;
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(sftp, remote,
                                                     LIBSSH2_FXF_READ, 0);
    if (!handle) {
        return false;
    }

    FILE *f = fopen(local, "wb");
    if (!f) {
        libssh2_sftp_close(handle);
        return false;
    }

    char buffer[32768];
    ssize_t nread;

    while ((nread = libssh2_sftp_read(handle, buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, nread, f) != (size_t)nread) {
            fclose(f);
            libssh2_sftp_close(handle);
            unlink(local);
            return false;
        }
    }

    fclose(f);
    libssh2_sftp_close(handle);

    return nread == 0;
}

bool sftp_upload(NetworkConnection *conn, const char *local, const char *remote)
{
    if (!conn->sftp_session) {
        return false;
    }

    FILE *f = fopen(local, "rb");
    if (!f) {
        return false;
    }

    LIBSSH2_SFTP *sftp = (LIBSSH2_SFTP*)conn->sftp_session;
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(sftp, remote,
                                                     LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                                                     LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
                                                     LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!handle) {
        fclose(f);
        return false;
    }

    char buffer[32768];
    size_t nread;
    bool success = true;

    while ((nread = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        ssize_t nwritten = libssh2_sftp_write(handle, buffer, nread);
        if (nwritten != (ssize_t)nread) {
            success = false;
            break;
        }
    }

    // Check for file error before closing
    bool had_error = ferror(f);
    fclose(f);
    libssh2_sftp_close(handle);

    return success && !had_error;
}

bool sftp_mkdir(NetworkConnection *conn, const char *path)
{
    if (!conn->sftp_session) {
        return false;
    }

    LIBSSH2_SFTP *sftp = (LIBSSH2_SFTP*)conn->sftp_session;
    int rc = libssh2_sftp_mkdir(sftp, path,
                                 LIBSSH2_SFTP_S_IRWXU |
                                 LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IXGRP |
                                 LIBSSH2_SFTP_S_IROTH | LIBSSH2_SFTP_S_IXOTH);
    return rc == 0;
}

bool sftp_rmdir(NetworkConnection *conn, const char *path)
{
    if (!conn->sftp_session) {
        return false;
    }

    LIBSSH2_SFTP *sftp = (LIBSSH2_SFTP*)conn->sftp_session;
    int rc = libssh2_sftp_rmdir(sftp, path);
    return rc == 0;
}

bool sftp_unlink(NetworkConnection *conn, const char *path)
{
    if (!conn->sftp_session) {
        return false;
    }

    LIBSSH2_SFTP *sftp = (LIBSSH2_SFTP*)conn->sftp_session;
    int rc = libssh2_sftp_unlink(sftp, path);
    return rc == 0;
}

bool sftp_rename(NetworkConnection *conn, const char *old_path, const char *new_path)
{
    if (!conn->sftp_session) {
        return false;
    }

    LIBSSH2_SFTP *sftp = (LIBSSH2_SFTP*)conn->sftp_session;
    int rc = libssh2_sftp_rename(sftp, old_path, new_path);
    return rc == 0;
}

// SMB stubs - would need samba/libsmbclient implementation

bool smb_connect(NetworkConnection *conn)
{
    strncpy(conn->error_message, "SMB support not yet implemented", sizeof(conn->error_message) - 1);
    return false;
}

void smb_disconnect(NetworkConnection *conn)
{
    (void)conn;
}

bool smb_read_directory(NetworkConnection *conn, const char *path, DirectoryState *dir)
{
    (void)conn;
    (void)path;
    (void)dir;
    return false;
}

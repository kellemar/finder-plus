#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test framework imports
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

#include "../src/core/network.h"

static void test_network_manager_init(void)
{
    NetworkManager mgr;
    bool result = network_init(&mgr);

    TEST_ASSERT(result, "Network manager should initialize successfully");
    TEST_ASSERT(mgr.initialized, "Manager should be marked as initialized");
    TEST_ASSERT_EQ(-1, mgr.active_connection, "No active connection by default");
    TEST_ASSERT_EQ(0, mgr.connection_count, "No connections by default");
    TEST_ASSERT_EQ(0, mgr.profile_count, "No profiles by default");

    network_shutdown(&mgr);
    TEST_ASSERT(!mgr.initialized, "Manager should be marked as not initialized after shutdown");
}

static void test_connection_type_name(void)
{
    TEST_ASSERT_STR_EQ("None", network_connection_type_name(CONN_TYPE_NONE), "CONN_TYPE_NONE name");
    TEST_ASSERT_STR_EQ("SFTP", network_connection_type_name(CONN_TYPE_SFTP), "CONN_TYPE_SFTP name");
    TEST_ASSERT_STR_EQ("SMB", network_connection_type_name(CONN_TYPE_SMB), "CONN_TYPE_SMB name");
}

static void test_connection_status_name(void)
{
    TEST_ASSERT_STR_EQ("Disconnected", network_status_name(CONN_STATUS_DISCONNECTED), "CONN_STATUS_DISCONNECTED name");
    TEST_ASSERT_STR_EQ("Connecting", network_status_name(CONN_STATUS_CONNECTING), "CONN_STATUS_CONNECTING name");
    TEST_ASSERT_STR_EQ("Connected", network_status_name(CONN_STATUS_CONNECTED), "CONN_STATUS_CONNECTED name");
    TEST_ASSERT_STR_EQ("Error", network_status_name(CONN_STATUS_ERROR), "CONN_STATUS_ERROR name");
    TEST_ASSERT_STR_EQ("Reconnecting", network_status_name(CONN_STATUS_RECONNECTING), "CONN_STATUS_RECONNECTING name");
}

static void test_profile_validation(void)
{
    ConnectionProfile profile;
    char error[256];

    // Empty profile should fail
    memset(&profile, 0, sizeof(profile));
    TEST_ASSERT(!network_validate_profile(&profile, error, sizeof(error)), "Empty profile should fail validation");

    // Profile without host should fail
    profile.type = CONN_TYPE_SFTP;
    profile.host[0] = '\0';
    TEST_ASSERT(!network_validate_profile(&profile, error, sizeof(error)), "Profile without host should fail");

    // Profile without username should fail
    strncpy(profile.host, "example.com", sizeof(profile.host) - 1);
    profile.username[0] = '\0';
    TEST_ASSERT(!network_validate_profile(&profile, error, sizeof(error)), "Profile without username should fail");

    // Valid profile should pass
    strncpy(profile.username, "user", sizeof(profile.username) - 1);
    TEST_ASSERT(network_validate_profile(&profile, error, sizeof(error)), "Valid profile should pass validation");
}

static void test_profile_management(void)
{
    NetworkManager mgr;
    network_init(&mgr);

    ConnectionProfile profile;
    memset(&profile, 0, sizeof(profile));
    strncpy(profile.name, "Test Server", sizeof(profile.name) - 1);
    profile.type = CONN_TYPE_SFTP;
    strncpy(profile.host, "test.example.com", sizeof(profile.host) - 1);
    profile.port = 22;
    strncpy(profile.username, "testuser", sizeof(profile.username) - 1);

    // Add profile
    int index = network_add_profile(&mgr, &profile);
    TEST_ASSERT(index >= 0, "Should add profile successfully");
    TEST_ASSERT_EQ(1, mgr.profile_count, "Profile count should be 1");

    // Get profile
    ConnectionProfile *retrieved = network_get_profile(&mgr, index);
    TEST_ASSERT(retrieved != NULL, "Should retrieve profile");
    TEST_ASSERT_STR_EQ("Test Server", retrieved->name, "Profile name should match");
    TEST_ASSERT_STR_EQ("test.example.com", retrieved->host, "Profile host should match");

    // Update profile
    strncpy(profile.name, "Updated Server", sizeof(profile.name) - 1);
    bool updated = network_update_profile(&mgr, index, &profile);
    TEST_ASSERT(updated, "Should update profile");
    retrieved = network_get_profile(&mgr, index);
    TEST_ASSERT_STR_EQ("Updated Server", retrieved->name, "Updated name should match");

    // Remove profile
    bool removed = network_remove_profile(&mgr, index);
    TEST_ASSERT(removed, "Should remove profile");
    TEST_ASSERT_EQ(0, mgr.profile_count, "Profile count should be 0 after removal");

    // Get invalid profile
    retrieved = network_get_profile(&mgr, 999);
    TEST_ASSERT(retrieved == NULL, "Should return NULL for invalid index");

    network_shutdown(&mgr);
}

static void test_active_connection(void)
{
    NetworkManager mgr;
    network_init(&mgr);

    TEST_ASSERT_EQ(-1, network_get_active(&mgr), "No active connection by default");
    TEST_ASSERT(!network_is_remote_active(&mgr), "Remote should not be active by default");

    network_set_active(&mgr, 5);
    TEST_ASSERT_EQ(5, network_get_active(&mgr), "Should set active connection");
    TEST_ASSERT(network_is_remote_active(&mgr), "Remote should be active after setting");

    network_set_active(&mgr, -1);
    TEST_ASSERT(!network_is_remote_active(&mgr), "Remote should not be active after clearing");

    network_shutdown(&mgr);
}

static void test_connection_get_null(void)
{
    NetworkManager mgr;
    network_init(&mgr);

    NetworkConnection *conn = network_get_connection(&mgr, 999);
    TEST_ASSERT(conn == NULL, "Should return NULL for non-existent connection");

    ConnectionStatus status = network_get_status(&mgr, 999);
    TEST_ASSERT_EQ(CONN_STATUS_DISCONNECTED, status, "Status should be disconnected for non-existent connection");

    network_shutdown(&mgr);
}

void test_network(void)
{
    test_network_manager_init();
    test_connection_type_name();
    test_connection_status_name();
    test_profile_validation();
    test_profile_management();
    test_active_connection();
    test_connection_get_null();
}

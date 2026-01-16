#ifndef GIT_H
#define GIT_H

#include <stdbool.h>
#include <stddef.h>

#define GIT_BRANCH_MAX_LEN 128
#define GIT_PATH_MAX_LEN 4096

// Git file status (matches git status --porcelain output)
typedef enum GitFileStatus {
    GIT_STATUS_NONE = 0,        // Not in a git repo or untracked by design
    GIT_STATUS_UNTRACKED,       // ? - New file not yet tracked
    GIT_STATUS_MODIFIED,        // M - Modified (unstaged)
    GIT_STATUS_STAGED,          // A/M - Staged for commit
    GIT_STATUS_DELETED,         // D - Deleted
    GIT_STATUS_RENAMED,         // R - Renamed
    GIT_STATUS_CONFLICT,        // U - Unmerged/conflict
    GIT_STATUS_IGNORED          // ! - Ignored by .gitignore
} GitFileStatus;

// Git repository state
typedef struct GitState {
    bool is_repo;                           // Is current directory in a git repo
    char repo_root[GIT_PATH_MAX_LEN];       // Root path of the repository
    char branch[GIT_BRANCH_MAX_LEN];        // Current branch name
    bool is_detached;                       // HEAD is detached
    int ahead;                              // Commits ahead of remote
    int behind;                             // Commits behind remote
    bool has_stash;                         // Has stashed changes
    bool has_staged;                        // Has staged changes
    bool has_modified;                      // Has modified files
    bool has_untracked;                     // Has untracked files
} GitState;

// Git file status entry for a single file
typedef struct GitFileStatusEntry {
    char path[GIT_PATH_MAX_LEN];
    GitFileStatus status;
    GitFileStatus staged_status;            // Status in index (staged area)
} GitFileStatusEntry;

// Git status result for a directory
typedef struct GitStatusResult {
    GitFileStatusEntry *entries;
    int count;
    int capacity;
} GitStatusResult;

// Initialize git state
void git_state_init(GitState *state);

// Free git state resources
void git_state_free(GitState *state);

// Check if a path is inside a git repository
bool git_is_repo(const char *path);

// Get the root directory of the git repository containing path
bool git_get_repo_root(const char *path, char *root, size_t root_size);

// Update git state for the given directory
bool git_update_state(GitState *state, const char *path);

// Get current branch name
bool git_get_branch(const char *repo_path, char *branch, size_t branch_size);

// Initialize git status result
void git_status_result_init(GitStatusResult *result);

// Free git status result
void git_status_result_free(GitStatusResult *result);

// Get git status for files in a directory
bool git_get_status(const char *path, GitStatusResult *result);

// Get status for a single file (relative to repo root)
GitFileStatus git_get_file_status(const GitStatusResult *result, const char *filename);

// Get diff for a file
bool git_get_diff(const char *repo_path, const char *file_path, char *diff, size_t diff_size);

// Get diff stats (lines added/removed)
bool git_get_diff_stats(const char *repo_path, const char *file_path, int *added, int *removed);

// Quick commit (stage all and commit)
bool git_quick_commit(const char *repo_path, const char *message);

// Stage a single file
bool git_stage_file(const char *repo_path, const char *file_path);

// Unstage a single file
bool git_unstage_file(const char *repo_path, const char *file_path);

// Get status character for display
char git_status_char(GitFileStatus status);

// Get status display string
const char* git_status_string(GitFileStatus status);

#endif // GIT_H

#include "git.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Run a git command and capture output
static bool run_git_command(const char *repo_path, const char *args, char *output, size_t output_size)
{
    char command[GIT_PATH_MAX_LEN * 2];

    if (repo_path && repo_path[0] != '\0') {
        snprintf(command, sizeof(command), "cd \"%s\" && git %s 2>/dev/null", repo_path, args);
    } else {
        snprintf(command, sizeof(command), "git %s 2>/dev/null", args);
    }

    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        return false;
    }

    if (output != NULL && output_size > 0) {
        output[0] = '\0';
        size_t total_read = 0;

        while (total_read < output_size - 1) {
            size_t bytes_read = fread(output + total_read, 1, output_size - 1 - total_read, fp);
            if (bytes_read == 0) break;
            total_read += bytes_read;
        }
        output[total_read] = '\0';

        // Trim trailing whitespace
        while (total_read > 0 && isspace((unsigned char)output[total_read - 1])) {
            output[--total_read] = '\0';
        }
    }

    int status = pclose(fp);
    return (status == 0);
}

// Check if git command succeeded (ignoring output)
static bool run_git_check(const char *repo_path, const char *args)
{
    char output[256];
    return run_git_command(repo_path, args, output, sizeof(output));
}

void git_state_init(GitState *state)
{
    memset(state, 0, sizeof(GitState));
}

void git_state_free(GitState *state)
{
    // Currently no dynamic allocation, but keep for future
    memset(state, 0, sizeof(GitState));
}

bool git_is_repo(const char *path)
{
    char output[256];
    return run_git_command(path, "rev-parse --is-inside-work-tree", output, sizeof(output)) &&
           strcmp(output, "true") == 0;
}

bool git_get_repo_root(const char *path, char *root, size_t root_size)
{
    return run_git_command(path, "rev-parse --show-toplevel", root, root_size);
}

bool git_get_branch(const char *repo_path, char *branch, size_t branch_size)
{
    // Try to get symbolic ref first (normal branch)
    if (run_git_command(repo_path, "symbolic-ref --short HEAD", branch, branch_size)) {
        return true;
    }

    // If that fails, we're in detached HEAD state - get short hash
    if (run_git_command(repo_path, "rev-parse --short HEAD", branch, branch_size)) {
        // Prepend indicator for detached state
        char temp[GIT_BRANCH_MAX_LEN];
        strncpy(temp, branch, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
        snprintf(branch, branch_size, "(%s)", temp);
        return true;
    }

    return false;
}

bool git_update_state(GitState *state, const char *path)
{
    git_state_init(state);

    // Check if in a git repo
    if (!git_is_repo(path)) {
        return false;
    }
    state->is_repo = true;

    // Get repo root
    if (!git_get_repo_root(path, state->repo_root, sizeof(state->repo_root))) {
        return false;
    }

    // Get current branch (reuse git_get_branch to avoid duplication)
    if (git_get_branch(path, state->branch, sizeof(state->branch))) {
        state->is_detached = (state->branch[0] == '(');
    }

    // Get ahead/behind counts from tracking branch
    char ahead_behind[128];
    if (run_git_command(path, "rev-list --left-right --count @{upstream}...HEAD", ahead_behind, sizeof(ahead_behind))) {
        sscanf(ahead_behind, "%d\t%d", &state->behind, &state->ahead);
    }

    // Check for stash
    char stash_output[64];
    state->has_stash = run_git_command(path, "stash list", stash_output, sizeof(stash_output)) &&
                       stash_output[0] != '\0';

    // Check for staged/modified/untracked using status
    char status_output[4096];
    if (run_git_command(path, "status --porcelain", status_output, sizeof(status_output))) {
        char *line = status_output;
        while (*line) {
            if (line[0] != ' ' && line[0] != '?') {
                state->has_staged = true;
            }
            if (line[1] == 'M' || line[1] == 'D') {
                state->has_modified = true;
            }
            if (line[0] == '?') {
                state->has_untracked = true;
            }

            // Move to next line
            char *newline = strchr(line, '\n');
            if (newline == NULL) break;
            line = newline + 1;
        }
    }

    return true;
}

void git_status_result_init(GitStatusResult *result)
{
    result->entries = NULL;
    result->count = 0;
    result->capacity = 0;
}

void git_status_result_free(GitStatusResult *result)
{
    free(result->entries);
    result->entries = NULL;
    result->count = 0;
    result->capacity = 0;
}

// Parse a single status line from git status --porcelain
static bool parse_status_line(const char *line, GitFileStatusEntry *entry)
{
    if (strlen(line) < 4) {
        return false;
    }

    // Format: XY filename
    // X = index status (staged)
    // Y = worktree status (unstaged)
    char index_status = line[0];
    char worktree_status = line[1];
    const char *filename = line + 3;

    // Skip the space
    if (line[2] != ' ') {
        return false;
    }

    // Copy filename (handle quoted paths and renames)
    const char *arrow = strstr(filename, " -> ");
    if (arrow != NULL) {
        // Rename: use the new name
        filename = arrow + 4;
    }

    // Remove quotes if present
    if (filename[0] == '"') {
        filename++;
        size_t len = strlen(filename);
        strncpy(entry->path, filename, sizeof(entry->path) - 1);
        if (len > 0 && entry->path[len - 1] == '"') {
            entry->path[len - 1] = '\0';
        }
    } else {
        strncpy(entry->path, filename, sizeof(entry->path) - 1);
    }
    entry->path[sizeof(entry->path) - 1] = '\0';

    // Parse index (staged) status
    switch (index_status) {
        case 'A': entry->staged_status = GIT_STATUS_STAGED; break;
        case 'M': entry->staged_status = GIT_STATUS_STAGED; break;
        case 'D': entry->staged_status = GIT_STATUS_DELETED; break;
        case 'R': entry->staged_status = GIT_STATUS_RENAMED; break;
        case 'U': entry->staged_status = GIT_STATUS_CONFLICT; break;
        case '!': entry->staged_status = GIT_STATUS_IGNORED; break;
        default: entry->staged_status = GIT_STATUS_NONE; break;
    }

    // Parse worktree (unstaged) status
    switch (worktree_status) {
        case 'M': entry->status = GIT_STATUS_MODIFIED; break;
        case 'D': entry->status = GIT_STATUS_DELETED; break;
        case 'U': entry->status = GIT_STATUS_CONFLICT; break;
        case '?': entry->status = GIT_STATUS_UNTRACKED; break;
        case '!': entry->status = GIT_STATUS_IGNORED; break;
        default:
            // If no worktree status but has staged status, show as staged
            if (entry->staged_status != GIT_STATUS_NONE) {
                entry->status = entry->staged_status;
            } else {
                entry->status = GIT_STATUS_NONE;
            }
            break;
    }

    // Untracked files have '?' in both columns
    if (index_status == '?' && worktree_status == '?') {
        entry->status = GIT_STATUS_UNTRACKED;
        entry->staged_status = GIT_STATUS_NONE;
    }

    return true;
}

bool git_get_status(const char *path, GitStatusResult *result)
{
    git_status_result_init(result);

    // Get status output
    char status_output[65536];  // Large buffer for directories with many files
    if (!run_git_command(path, "status --porcelain -uall", status_output, sizeof(status_output))) {
        return false;
    }

    // Count lines to pre-allocate
    int line_count = 0;
    for (const char *p = status_output; *p; p++) {
        if (*p == '\n') line_count++;
    }
    if (status_output[0] != '\0' && status_output[strlen(status_output) - 1] != '\n') {
        line_count++;  // Last line without newline
    }

    if (line_count == 0) {
        return true;  // No changes, success
    }

    // Allocate entries
    result->entries = calloc(line_count, sizeof(GitFileStatusEntry));
    if (result->entries == NULL) {
        return false;
    }
    result->capacity = line_count;

    // Parse each line
    char *line = status_output;
    while (*line) {
        char *newline = strchr(line, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }

        if (strlen(line) > 0) {
            GitFileStatusEntry entry;
            if (parse_status_line(line, &entry)) {
                result->entries[result->count++] = entry;
            }
        }

        if (newline == NULL) break;
        line = newline + 1;
    }

    return true;
}

GitFileStatus git_get_file_status(const GitStatusResult *result, const char *filename)
{
    if (result == NULL || result->entries == NULL || filename == NULL) {
        return GIT_STATUS_NONE;
    }

    // Look for exact match or match at end of path
    for (int i = 0; i < result->count; i++) {
        const char *entry_path = result->entries[i].path;

        // Exact match
        if (strcmp(entry_path, filename) == 0) {
            return result->entries[i].status;
        }

        // Match filename at end of path (for when we have full path but entry has relative)
        size_t filename_len = strlen(filename);
        size_t entry_len = strlen(entry_path);

        if (entry_len < filename_len) {
            // Check if filename ends with entry_path
            const char *suffix = filename + filename_len - entry_len;
            if (suffix > filename && suffix[-1] == '/' && strcmp(suffix, entry_path) == 0) {
                return result->entries[i].status;
            }
        } else if (entry_len > filename_len) {
            // Check if entry_path ends with filename
            const char *suffix = entry_path + entry_len - filename_len;
            if (suffix > entry_path && suffix[-1] == '/' && strcmp(suffix, filename) == 0) {
                return result->entries[i].status;
            }
        }
    }

    return GIT_STATUS_NONE;
}

bool git_get_diff(const char *repo_path, const char *file_path, char *diff, size_t diff_size)
{
    char args[GIT_PATH_MAX_LEN + 32];
    snprintf(args, sizeof(args), "diff -- \"%s\"", file_path);

    if (!run_git_command(repo_path, args, diff, diff_size)) {
        // Try diff for staged changes
        snprintf(args, sizeof(args), "diff --cached -- \"%s\"", file_path);
        return run_git_command(repo_path, args, diff, diff_size);
    }

    return true;
}

bool git_get_diff_stats(const char *repo_path, const char *file_path, int *added, int *removed)
{
    *added = 0;
    *removed = 0;

    char args[GIT_PATH_MAX_LEN + 64];
    snprintf(args, sizeof(args), "diff --numstat -- \"%s\"", file_path);

    char output[256];
    if (run_git_command(repo_path, args, output, sizeof(output))) {
        if (sscanf(output, "%d\t%d", added, removed) == 2) {
            return true;
        }
    }

    // Try staged diff
    snprintf(args, sizeof(args), "diff --cached --numstat -- \"%s\"", file_path);
    if (run_git_command(repo_path, args, output, sizeof(output))) {
        sscanf(output, "%d\t%d", added, removed);
        return true;
    }

    return false;
}

bool git_quick_commit(const char *repo_path, const char *message)
{
    char args[1024];

    // Stage all changes
    if (!run_git_check(repo_path, "add -A")) {
        return false;
    }

    // Commit with message
    snprintf(args, sizeof(args), "commit -m \"%s\"", message);
    return run_git_check(repo_path, args);
}

bool git_stage_file(const char *repo_path, const char *file_path)
{
    char args[GIT_PATH_MAX_LEN + 16];
    snprintf(args, sizeof(args), "add -- \"%s\"", file_path);
    return run_git_check(repo_path, args);
}

bool git_unstage_file(const char *repo_path, const char *file_path)
{
    char args[GIT_PATH_MAX_LEN + 32];
    snprintf(args, sizeof(args), "reset HEAD -- \"%s\"", file_path);
    return run_git_check(repo_path, args);
}

char git_status_char(GitFileStatus status)
{
    switch (status) {
        case GIT_STATUS_UNTRACKED: return '?';
        case GIT_STATUS_MODIFIED:  return 'M';
        case GIT_STATUS_STAGED:    return 'A';
        case GIT_STATUS_DELETED:   return 'D';
        case GIT_STATUS_RENAMED:   return 'R';
        case GIT_STATUS_CONFLICT:  return 'U';
        case GIT_STATUS_IGNORED:   return '!';
        default: return ' ';
    }
}

const char* git_status_string(GitFileStatus status)
{
    switch (status) {
        case GIT_STATUS_UNTRACKED: return "Untracked";
        case GIT_STATUS_MODIFIED:  return "Modified";
        case GIT_STATUS_STAGED:    return "Staged";
        case GIT_STATUS_DELETED:   return "Deleted";
        case GIT_STATUS_RENAMED:   return "Renamed";
        case GIT_STATUS_CONFLICT:  return "Conflict";
        case GIT_STATUS_IGNORED:   return "Ignored";
        default: return "";
    }
}

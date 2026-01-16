#include "statusbar.h"
#include "../app.h"
#include "../core/filesystem.h"
#include "../core/git.h"
#include "../core/operation_queue.h"
#include "../utils/theme.h"
#include "../utils/font.h"
#include "../utils/perf.h"

#include <stdio.h>
#include <string.h>

void statusbar_draw(App *app)
{
    int y = app->height - STATUSBAR_HEIGHT;

    // Background
    DrawRectangle(0, y, app->width, STATUSBAR_HEIGHT, g_theme.sidebar);

    // Top border
    DrawLine(0, y, app->width, y, g_theme.border);

    int text_y = y + (STATUSBAR_HEIGHT - FONT_SIZE_SMALL) / 2;
    int x = PADDING;

    // Item count
    char items_str[64];
    if (app->directory.count == 1) {
        snprintf(items_str, sizeof(items_str), "1 item");
    } else {
        snprintf(items_str, sizeof(items_str), "%d items", app->directory.count);
    }
    DrawTextCustom(items_str, x, text_y, FONT_SIZE_SMALL, g_theme.textSecondary);
    x += MeasureTextCustom(items_str, FONT_SIZE_SMALL) + PADDING * 2;

    // Show operation queue progress if there are pending/active operations
    int pending = operation_queue_pending_count(&app->op_queue);
    bool is_processing = operation_queue_is_processing(&app->op_queue);

    if (pending > 0 || is_processing) {
        // Draw progress bar and status
        int progress = operation_queue_overall_progress(&app->op_queue);

        // Progress bar background
        int bar_width = 100;
        int bar_height = 10;
        int bar_y = text_y + (FONT_SIZE_SMALL - bar_height) / 2;
        DrawRectangle(x, bar_y, bar_width, bar_height, g_theme.hover);

        // Progress bar fill
        int fill_width = (bar_width * progress) / 100;
        DrawRectangle(x, bar_y, fill_width, bar_height, g_theme.accent);
        x += bar_width + PADDING;

        // Operation text
        char op_status[64];
        if (is_processing) {
            QueuedOperation *current = operation_queue_current(&app->op_queue);
            if (current) {
                const char *op_name = queue_op_type_name(current->type);
                snprintf(op_status, sizeof(op_status), "%s %d%%", op_name, progress);
            } else {
                snprintf(op_status, sizeof(op_status), "Working... %d%%", progress);
            }
        } else {
            snprintf(op_status, sizeof(op_status), "%d queued", pending);
        }
        DrawTextCustom(op_status, x, text_y, FONT_SIZE_SMALL, g_theme.accent);
        x += MeasureTextCustom(op_status, FONT_SIZE_SMALL) + PADDING * 2;

        // Separator
        DrawTextCustom("|", x, text_y, FONT_SIZE_SMALL, g_theme.border);
        x += MeasureTextCustom("|", FONT_SIZE_SMALL) + PADDING * 2;
    } else {
        // Separator
        DrawTextCustom("|", x, text_y, FONT_SIZE_SMALL, g_theme.border);
        x += MeasureTextCustom("|", FONT_SIZE_SMALL) + PADDING * 2;
    }

    // Current path (truncated if needed)
    const char *path = app->directory.current_path;
    int max_path_width = app->width - x - 200; // Leave space for right side info

    char display_path[256];
    int path_width = MeasureTextCustom(path, FONT_SIZE_SMALL);

    if (path_width > max_path_width) {
        // Truncate from the left
        int chars_to_show = (max_path_width / 8) - 3; // Approximate
        if (chars_to_show < 10) chars_to_show = 10;

        int path_len = strlen(path);
        if (chars_to_show < path_len) {
            snprintf(display_path, sizeof(display_path), "...%s", path + (path_len - chars_to_show));
        } else {
            strncpy(display_path, path, sizeof(display_path) - 1);
            display_path[sizeof(display_path) - 1] = '\0';
        }
    } else {
        strncpy(display_path, path, sizeof(display_path) - 1);
        display_path[sizeof(display_path) - 1] = '\0';
    }

    DrawTextCustom(display_path, x, text_y, FONT_SIZE_SMALL, g_theme.textPrimary);

    // Right side: Git branch, Free disk space and FPS
    char right_info[256];

    off_t free_space = get_free_disk_space(app->directory.current_path);
    char free_space_str[32];
    if (free_space >= 0) {
        format_file_size(free_space, free_space_str, sizeof(free_space_str));
    } else {
        strncpy(free_space_str, "N/A", sizeof(free_space_str));
    }

    // Build right info string
    if (app->git.is_repo && app->git.branch[0] != '\0') {
        // Show git branch with status indicators
        char git_status[32] = "";
        if (app->git.has_staged) strcat(git_status, "+");
        if (app->git.has_modified) strcat(git_status, "*");
        if (app->git.has_untracked) strcat(git_status, "?");

        if (app->git.ahead > 0 || app->git.behind > 0) {
            char ahead_behind[32];
            snprintf(ahead_behind, sizeof(ahead_behind), " ↑%d↓%d", app->git.ahead, app->git.behind);
            strcat(git_status, ahead_behind);
        }

        snprintf(right_info, sizeof(right_info), "%s%s | %s free | %s | %.0f FPS",
                 app->git.branch, git_status, free_space_str, view_mode_name(app->view_mode), app->fps);
    } else {
        snprintf(right_info, sizeof(right_info), "%s free | %s | %.0f FPS",
                 free_space_str, view_mode_name(app->view_mode), app->fps);
    }

    int right_width = MeasureTextCustom(right_info, FONT_SIZE_SMALL);
    int right_x = app->width - right_width - PADDING;

    // Draw git branch in accent color if in repo
    if (app->git.is_repo && app->git.branch[0] != '\0') {
        // Calculate branch part width
        char git_part[128];
        char git_status[32] = "";
        if (app->git.has_staged) strcat(git_status, "+");
        if (app->git.has_modified) strcat(git_status, "*");
        if (app->git.has_untracked) strcat(git_status, "?");
        if (app->git.ahead > 0 || app->git.behind > 0) {
            char ahead_behind[32];
            snprintf(ahead_behind, sizeof(ahead_behind), " ↑%d↓%d", app->git.ahead, app->git.behind);
            strcat(git_status, ahead_behind);
        }
        snprintf(git_part, sizeof(git_part), "%s%s", app->git.branch, git_status);
        int git_width = MeasureTextCustom(git_part, FONT_SIZE_SMALL);

        // Draw git branch in green/accent color
        Color branch_color = app->git.has_modified ? g_theme.gitModified :
                            (app->git.has_staged ? g_theme.gitStaged : g_theme.success);
        DrawTextCustom(git_part, right_x, text_y, FONT_SIZE_SMALL, branch_color);

        // Draw rest of info
        DrawTextCustom(" | ", right_x + git_width, text_y, FONT_SIZE_SMALL, g_theme.textSecondary);
        int sep_width = MeasureTextCustom(" | ", FONT_SIZE_SMALL);

        char rest_info[128];
        snprintf(rest_info, sizeof(rest_info), "%s free | %s | %.0f FPS",
                 free_space_str, view_mode_name(app->view_mode), app->fps);
        DrawTextCustom(rest_info, right_x + git_width + sep_width, text_y, FONT_SIZE_SMALL, g_theme.textSecondary);
    } else {
        DrawTextCustom(right_info, right_x, text_y, FONT_SIZE_SMALL, g_theme.textSecondary);
    }

    // Show hidden files indicator
    if (app->directory.show_hidden) {
        const char *hidden_indicator = "[H]";
        int indicator_x = right_x - MeasureTextCustom(hidden_indicator, FONT_SIZE_SMALL) - PADDING * 2;
        DrawTextCustom(hidden_indicator, indicator_x, text_y, FONT_SIZE_SMALL, g_theme.accent);
    }

    // Show performance stats overlay if enabled (Cmd+Shift+P to toggle)
    if (app->show_perf_stats) {
        char perf_str[256];
        perf_get_stats_string(&app->perf, perf_str, sizeof(perf_str));

        // Draw perf stats in a box at the top-right
        int perf_width = MeasureTextCustom(perf_str, FONT_SIZE_SMALL) + PADDING * 2;
        int perf_height = FONT_SIZE_SMALL + PADDING * 2;
        int perf_x = app->width - perf_width - PADDING;
        int perf_y = y - perf_height - 4;

        DrawRectangle(perf_x, perf_y, perf_width, perf_height, Fade(g_theme.background, 0.9f));
        DrawRectangleLines(perf_x, perf_y, perf_width, perf_height, g_theme.accent);
        DrawTextCustom(perf_str, perf_x + PADDING, perf_y + PADDING, FONT_SIZE_SMALL, g_theme.accent);
    }
}

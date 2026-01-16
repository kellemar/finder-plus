#include "queue_panel.h"
#include "../app.h"
#include "../core/operation_queue.h"
#include "../utils/font.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>

#define PANEL_COLLAPSED_HEIGHT 40
#define PANEL_EXPANDED_HEIGHT 200
#define PANEL_ANIMATION_SPEED 8.0f
#define QUEUE_ROW_HEIGHT 28
#define BUTTON_WIDTH 60
#define BUTTON_HEIGHT 24
#define QUEUE_PADDING 8

void queue_panel_init(QueuePanelState *panel)
{
    panel->visible = false;
    panel->expanded = false;
    panel->scroll_offset = 0;
    panel->selected_index = -1;
    panel->hovered_index = -1;
    panel->animation_progress = 0.0f;
}

void queue_panel_toggle(QueuePanelState *panel)
{
    if (panel->visible) {
        queue_panel_hide(panel);
    } else {
        queue_panel_show(panel);
    }
}

void queue_panel_show(QueuePanelState *panel)
{
    panel->visible = true;
}

void queue_panel_hide(QueuePanelState *panel)
{
    panel->visible = false;
    panel->expanded = false;
}

void queue_panel_update(QueuePanelState *panel, float delta_time)
{
    // Animate panel visibility
    float target = panel->visible ? 1.0f : 0.0f;
    if (panel->animation_progress < target) {
        panel->animation_progress += PANEL_ANIMATION_SPEED * delta_time;
        if (panel->animation_progress > target) {
            panel->animation_progress = target;
        }
    } else if (panel->animation_progress > target) {
        panel->animation_progress -= PANEL_ANIMATION_SPEED * delta_time;
        if (panel->animation_progress < target) {
            panel->animation_progress = target;
        }
    }
}

int queue_panel_get_height(QueuePanelState *panel)
{
    if (panel->animation_progress <= 0.0f) {
        return 0;
    }

    int target_height = panel->expanded ? PANEL_EXPANDED_HEIGHT : PANEL_COLLAPSED_HEIGHT;
    return (int)(target_height * panel->animation_progress);
}

bool queue_panel_is_visible(QueuePanelState *panel)
{
    return panel->visible || panel->animation_progress > 0.0f;
}

// Get filename from path for display
static const char* get_display_name(const char *path)
{
    const char *name = strrchr(path, '/');
    return name ? name + 1 : path;
}

// Draw progress bar
static void draw_progress_bar(int x, int y, int width, int height, int progress, Color bg, Color fill)
{
    DrawRectangle(x, y, width, height, bg);
    int fill_width = (width * progress) / 100;
    if (fill_width > 0) {
        DrawRectangle(x, y, fill_width, height, fill);
    }
}

// Draw a small button
static bool draw_button(int x, int y, int width, int height, const char *text, Color bg, Color hover_bg, Color text_color)
{
    Vector2 mouse = GetMousePosition();
    Rectangle rect = { (float)x, (float)y, (float)width, (float)height };
    bool hovered = CheckCollisionPointRec(mouse, rect);
    bool clicked = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    DrawRectangle(x, y, width, height, hovered ? hover_bg : bg);
    DrawRectangleLines(x, y, width, height, text_color);

    int text_width = MeasureTextCustom(text, 12);
    DrawTextCustom(text, x + (width - text_width) / 2, y + (height - 12) / 2, 12, text_color);

    return clicked;
}

void queue_panel_draw(struct App *app)
{
    if (!queue_panel_is_visible(&app->queue_panel)) {
        return;
    }

    Theme *theme = theme_get_current();
    int panel_height = queue_panel_get_height(&app->queue_panel);
    int panel_y = app->height - panel_height;
    int panel_width = app->width;

    // Draw panel background
    DrawRectangle(0, panel_y, panel_width, panel_height, theme->sidebar);
    DrawLine(0, panel_y, panel_width, panel_y, theme->border);

    if (panel_height < 20) {
        return; // Too small to draw content
    }

    OperationQueue *queue = &app->op_queue;

    // Header row
    int header_y = panel_y + 4;
    int pending = operation_queue_pending_count(queue);
    int total = operation_queue_total_count(queue);
    bool is_processing = operation_queue_is_processing(queue);
    bool is_paused = operation_queue_is_paused(queue);

    // Status text
    char status[128];
    if (is_paused) {
        snprintf(status, sizeof(status), "Queue Paused - %d pending", pending);
    } else if (is_processing) {
        snprintf(status, sizeof(status), "Processing... %d/%d", total - pending, total);
    } else if (pending > 0) {
        snprintf(status, sizeof(status), "Queued: %d operations", pending);
    } else {
        snprintf(status, sizeof(status), "Queue Empty");
    }

    DrawTextCustom(status, QUEUE_PADDING, header_y + 4, 14, theme->textPrimary);

    // Progress bar for overall progress
    int progress = operation_queue_overall_progress(queue);
    int progress_x = QUEUE_PADDING + MeasureTextCustom(status, 14) + 20;
    int progress_width = 150;
    draw_progress_bar(progress_x, header_y + 6, progress_width, 12, progress, theme->hover, theme->accent);

    // Control buttons
    int button_x = progress_x + progress_width + 20;

    // Expand/collapse button
    const char *expand_text = app->queue_panel.expanded ? "-" : "+";
    if (draw_button(button_x, header_y + 2, 24, 20, expand_text, theme->hover, theme->selection, theme->textPrimary)) {
        app->queue_panel.expanded = !app->queue_panel.expanded;
    }
    button_x += 30;

    // Pause/Resume button
    const char *pause_text = is_paused ? "Resume" : "Pause";
    if (draw_button(button_x, header_y + 2, BUTTON_WIDTH, 20, pause_text, theme->hover, theme->selection, theme->textPrimary)) {
        if (is_paused) {
            operation_queue_resume(queue);
        } else {
            operation_queue_pause(queue);
        }
    }
    button_x += BUTTON_WIDTH + 4;

    // Cancel All button
    if (pending > 0) {
        if (draw_button(button_x, header_y + 2, 70, 20, "Cancel All", theme->hover, theme->error, theme->textPrimary)) {
            operation_queue_cancel_all(queue);
        }
        button_x += 74;
    }

    // Clear finished button
    if (total > pending) {
        if (draw_button(button_x, header_y + 2, 50, 20, "Clear", theme->hover, theme->selection, theme->textPrimary)) {
            operation_queue_clear_finished(queue);
        }
    }

    // Close button
    if (draw_button(panel_width - 28, header_y + 2, 20, 20, "X", theme->hover, theme->error, theme->textPrimary)) {
        queue_panel_hide(&app->queue_panel);
    }

    // Expanded view - show operation list
    if (app->queue_panel.expanded && panel_height > PANEL_COLLAPSED_HEIGHT) {
        int list_y = panel_y + PANEL_COLLAPSED_HEIGHT;
        int list_height = panel_height - PANEL_COLLAPSED_HEIGHT;
        int visible_rows = list_height / QUEUE_ROW_HEIGHT;

        // Draw operation list
        for (int i = 0; i < queue->count && i < visible_rows + app->queue_panel.scroll_offset; i++) {
            if (i < app->queue_panel.scroll_offset) continue;

            int row_y = list_y + (i - app->queue_panel.scroll_offset) * QUEUE_ROW_HEIGHT;
            if (row_y + QUEUE_ROW_HEIGHT > app->height) break;

            QueuedOperation *op = &queue->operations[i];

            // Highlight selected/hovered row
            if (i == app->queue_panel.selected_index) {
                DrawRectangle(0, row_y, panel_width, QUEUE_ROW_HEIGHT, theme->selection);
            } else if (i == app->queue_panel.hovered_index) {
                DrawRectangle(0, row_y, panel_width, QUEUE_ROW_HEIGHT, theme->hover);
            }

            // Status icon/color
            Color status_color;
            const char *status_icon;
            switch (op->status) {
                case OP_STATUS_PENDING:
                    status_color = theme->textSecondary;
                    status_icon = ".";
                    break;
                case OP_STATUS_IN_PROGRESS:
                    status_color = theme->accent;
                    status_icon = ">";
                    break;
                case OP_STATUS_COMPLETED:
                    status_color = theme->success;
                    status_icon = "o";
                    break;
                case OP_STATUS_FAILED:
                    status_color = theme->error;
                    status_icon = "X";
                    break;
                case OP_STATUS_CANCELLED:
                    status_color = theme->textSecondary;
                    status_icon = "-";
                    break;
                default:
                    status_color = theme->textPrimary;
                    status_icon = "?";
                    break;
            }

            // Status icon
            DrawTextCustom(status_icon, QUEUE_PADDING, row_y + (QUEUE_ROW_HEIGHT - 12) / 2, 12, status_color);

            // Operation type
            const char *type_name = queue_op_type_name(op->type);
            DrawTextCustom(type_name, QUEUE_PADDING + 20, row_y + (QUEUE_ROW_HEIGHT - 12) / 2, 12, theme->textPrimary);

            // Source filename
            const char *source_name = get_display_name(op->source_path);
            DrawTextCustom(source_name, QUEUE_PADDING + 90, row_y + (QUEUE_ROW_HEIGHT - 12) / 2, 12, theme->textSecondary);

            // Progress for in-progress operations
            if (op->status == OP_STATUS_IN_PROGRESS) {
                draw_progress_bar(panel_width - 160, row_y + (QUEUE_ROW_HEIGHT - 10) / 2, 80, 10, op->progress, theme->hover, theme->accent);
            }

            // Cancel/Retry button for applicable operations
            if (op->status == OP_STATUS_PENDING) {
                if (draw_button(panel_width - 70, row_y + 2, 60, QUEUE_ROW_HEIGHT - 4, "Cancel", theme->hover, theme->error, theme->textPrimary)) {
                    operation_queue_cancel(queue, op->id);
                }
            } else if (op->status == OP_STATUS_FAILED && op->can_retry) {
                if (draw_button(panel_width - 70, row_y + 2, 60, QUEUE_ROW_HEIGHT - 4, "Retry", theme->hover, theme->warning, theme->textPrimary)) {
                    operation_queue_retry(queue, op->id);
                }
            }
        }

        // Scrollbar if needed
        if (queue->count > visible_rows) {
            int scrollbar_height = (visible_rows * list_height) / queue->count;
            int scrollbar_y = list_y + (app->queue_panel.scroll_offset * list_height) / queue->count;
            DrawRectangle(panel_width - 8, scrollbar_y, 6, scrollbar_height, theme->border);
        }
    }
}

void queue_panel_handle_input(struct App *app)
{
    if (!queue_panel_is_visible(&app->queue_panel)) {
        return;
    }

    int panel_height = queue_panel_get_height(&app->queue_panel);
    int panel_y = app->height - panel_height;

    Vector2 mouse = GetMousePosition();

    // Check if mouse is over panel
    if (mouse.y < panel_y) {
        app->queue_panel.hovered_index = -1;
        return;
    }

    // Handle scroll in expanded view
    if (app->queue_panel.expanded) {
        int scroll = (int)GetMouseWheelMove();
        if (scroll != 0) {
            app->queue_panel.scroll_offset -= scroll;
            int max_scroll = operation_queue_total_count(&app->op_queue) - (panel_height - PANEL_COLLAPSED_HEIGHT) / QUEUE_ROW_HEIGHT;
            if (app->queue_panel.scroll_offset < 0) app->queue_panel.scroll_offset = 0;
            if (app->queue_panel.scroll_offset > max_scroll) app->queue_panel.scroll_offset = max_scroll;
        }

        // Update hovered index
        int list_y = panel_y + PANEL_COLLAPSED_HEIGHT;
        if (mouse.y >= list_y) {
            int row_index = app->queue_panel.scroll_offset + (int)(mouse.y - list_y) / QUEUE_ROW_HEIGHT;
            if (row_index >= 0 && row_index < operation_queue_total_count(&app->op_queue)) {
                app->queue_panel.hovered_index = row_index;
            } else {
                app->queue_panel.hovered_index = -1;
            }
        } else {
            app->queue_panel.hovered_index = -1;
        }

        // Handle click to select
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && app->queue_panel.hovered_index >= 0) {
            app->queue_panel.selected_index = app->queue_panel.hovered_index;
        }
    }
}

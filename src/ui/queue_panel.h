#ifndef QUEUE_PANEL_H
#define QUEUE_PANEL_H

#include <stdbool.h>

// Forward declaration
struct App;

// Queue panel state
typedef struct QueuePanelState {
    bool visible;               // Is panel visible
    bool expanded;              // Is panel expanded (shows details)
    int scroll_offset;          // Scroll position
    int selected_index;         // Selected operation index
    int hovered_index;          // Mouse hover index
    float animation_progress;   // For slide animation 0-1
} QueuePanelState;

// Initialize queue panel state
void queue_panel_init(QueuePanelState *panel);

// Toggle panel visibility
void queue_panel_toggle(QueuePanelState *panel);

// Show/hide panel
void queue_panel_show(QueuePanelState *panel);
void queue_panel_hide(QueuePanelState *panel);

// Draw the queue panel
void queue_panel_draw(struct App *app);

// Handle queue panel input
void queue_panel_handle_input(struct App *app);

// Update queue panel animation
void queue_panel_update(QueuePanelState *panel, float delta_time);

// Get panel height
int queue_panel_get_height(QueuePanelState *panel);

// Check if panel is visible
bool queue_panel_is_visible(QueuePanelState *panel);

#endif // QUEUE_PANEL_H

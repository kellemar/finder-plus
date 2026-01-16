#ifndef SIDEBAR_H
#define SIDEBAR_H

// Forward declaration
struct App;

// Draw the sidebar
void sidebar_draw(struct App *app);

// Handle sidebar input (mouse clicks, resize)
void sidebar_handle_input(struct App *app);

// Get content area X offset (accounts for sidebar width)
int sidebar_get_content_x(struct App *app);

// Get content area width
int sidebar_get_content_width(struct App *app);

#endif // SIDEBAR_H

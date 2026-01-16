#ifndef BREADCRUMB_H
#define BREADCRUMB_H

#include <stdbool.h>

#define BREADCRUMB_HEIGHT 28
#define BREADCRUMB_MAX_SEGMENTS 32

// Breadcrumb segment
typedef struct BreadcrumbSegment {
    char name[64];
    char path[4096];
    int x;      // X position for click detection
    int width;  // Width of this segment
} BreadcrumbSegment;

// Breadcrumb state
typedef struct BreadcrumbState {
    BreadcrumbSegment segments[BREADCRUMB_MAX_SEGMENTS];
    int count;
    int hovered;    // Index of hovered segment (-1 if none)
    bool editing;   // Whether in edit mode (text input)
    char edit_buffer[4096];
    int edit_cursor;
} BreadcrumbState;

// Forward declaration
struct App;

// Initialize breadcrumb state
void breadcrumb_init(BreadcrumbState *breadcrumb);

// Update breadcrumb from current path
void breadcrumb_update(BreadcrumbState *breadcrumb, const char *path);

// Handle breadcrumb input (clicks, edit mode)
void breadcrumb_handle_input(struct App *app);

// Draw breadcrumb bar
void breadcrumb_draw(struct App *app);

// Get breadcrumb height
int breadcrumb_get_height(void);

// Check if in edit mode
bool breadcrumb_is_editing(BreadcrumbState *breadcrumb);

#endif // BREADCRUMB_H

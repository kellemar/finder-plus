#ifndef DIALOG_H
#define DIALOG_H

#include <stdbool.h>

// Forward declaration - must come before struct definition that uses it
struct App;

// Dialog types
typedef enum DialogType {
    DIALOG_NONE,
    DIALOG_CONFIRM,
    DIALOG_ERROR,
    DIALOG_INFO,
    DIALOG_SUMMARY     // Large scrollable dialog for AI summaries
} DialogType;

// Maximum summary size
#define DIALOG_SUMMARY_MAX 4096

// Dialog button selection
typedef enum DialogButton {
    DIALOG_BTN_OK,
    DIALOG_BTN_CANCEL
} DialogButton;

// Dialog callback type
typedef void (*DialogCallback)(struct App *);

// Dialog state
typedef struct DialogState {
    bool visible;
    DialogType type;
    char title[64];
    char message[512];
    DialogButton selected;
    DialogCallback on_confirm;  // Callback for confirm
    void *user_data;            // Optional context data

    // Summary dialog state (larger scrollable content)
    char *summary_content;      // Dynamically allocated summary text
    int summary_scroll;         // Scroll offset (line number)
    int summary_total_lines;    // Total wrapped lines
} DialogState;

// Initialize dialog state
void dialog_init(DialogState *dialog);

// Show a confirmation dialog
// Returns immediately; dialog handles itself until closed
void dialog_confirm(DialogState *dialog, const char *title, const char *message,
                    DialogCallback on_confirm);

// Show an error dialog
void dialog_error(DialogState *dialog, const char *title, const char *message);

// Show an info dialog
void dialog_info(DialogState *dialog, const char *title, const char *message);

// Show a summary dialog (large scrollable text for AI summaries)
void dialog_summary(DialogState *dialog, const char *title, const char *summary);

// Hide the dialog
void dialog_hide(DialogState *dialog);

// Check if dialog is visible
bool dialog_is_visible(DialogState *dialog);

// Handle dialog input
// Returns true if dialog consumed the input
bool dialog_handle_input(struct App *app);

// Draw the dialog
void dialog_draw(struct App *app);

#endif // DIALOG_H

#import <Cocoa/Cocoa.h>
#include "clipboard.h"
#include <string.h>
#include <stdlib.h>

// Static buffers for return values
#define MAX_PATH_LEN 4096
#define MAX_FILES 128
static char g_path_buffer[MAX_PATH_LEN];
static char g_text_buffer[8192];
static char *g_file_paths[MAX_FILES];
static int g_file_count = 0;

void platform_clipboard_init(void)
{
    // Nothing special needed for macOS
}

bool platform_clipboard_copy_files(const char **paths, int count)
{
    if (count <= 0 || paths == NULL) {
        return false;
    }

    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];

    NSMutableArray *urls = [NSMutableArray arrayWithCapacity:count];

    for (int i = 0; i < count; i++) {
        if (paths[i] == NULL) continue;

        NSString *pathStr = [NSString stringWithUTF8String:paths[i]];
        NSURL *url = [NSURL fileURLWithPath:pathStr];
        if (url) {
            [urls addObject:url];
        }
    }

    if ([urls count] == 0) {
        return false;
    }

    // Write file URLs to pasteboard
    return [pasteboard writeObjects:urls];
}

bool platform_clipboard_has_files(void)
{
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSArray *classes = @[[NSURL class]];
    NSDictionary *options = @{NSPasteboardURLReadingFileURLsOnlyKey: @YES};

    return [pasteboard canReadObjectForClasses:classes options:options];
}

// Helper to clear cached file paths
static void clear_cached_paths(void)
{
    for (int i = 0; i < g_file_count; i++) {
        if (g_file_paths[i]) {
            free(g_file_paths[i]);
            g_file_paths[i] = NULL;
        }
    }
    g_file_count = 0;
}

// Helper to load file paths from pasteboard
static void load_file_paths(void)
{
    clear_cached_paths();

    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSArray *classes = @[[NSURL class]];
    NSDictionary *options = @{NSPasteboardURLReadingFileURLsOnlyKey: @YES};

    NSArray *urls = [pasteboard readObjectsForClasses:classes options:options];

    if (urls == nil) {
        return;
    }

    for (NSURL *url in urls) {
        if (g_file_count >= MAX_FILES) break;

        NSString *path = [url path];
        if (path) {
            const char *cpath = [path UTF8String];
            if (cpath) {
                g_file_paths[g_file_count] = strdup(cpath);
                if (g_file_paths[g_file_count]) {
                    g_file_count++;
                }
            }
        }
    }
}

int platform_clipboard_get_file_count(void)
{
    load_file_paths();
    return g_file_count;
}

const char* platform_clipboard_get_file_path(int index)
{
    // Reload if needed
    if (g_file_count == 0) {
        load_file_paths();
    }

    if (index < 0 || index >= g_file_count) {
        return NULL;
    }

    // Copy to static buffer for safety
    if (g_file_paths[index]) {
        strncpy(g_path_buffer, g_file_paths[index], MAX_PATH_LEN - 1);
        g_path_buffer[MAX_PATH_LEN - 1] = '\0';
        return g_path_buffer;
    }

    return NULL;
}

bool platform_clipboard_copy_text(const char *text)
{
    if (text == NULL) {
        return false;
    }

    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];

    NSString *str = [NSString stringWithUTF8String:text];
    if (str == nil) {
        return false;
    }

    return [pasteboard setString:str forType:NSPasteboardTypeString];
}

const char* platform_clipboard_get_text(void)
{
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSString *text = [pasteboard stringForType:NSPasteboardTypeString];

    if (text == nil) {
        return NULL;
    }

    const char *ctext = [text UTF8String];
    if (ctext == NULL) {
        return NULL;
    }

    strncpy(g_text_buffer, ctext, sizeof(g_text_buffer) - 1);
    g_text_buffer[sizeof(g_text_buffer) - 1] = '\0';

    return g_text_buffer;
}

void platform_clipboard_clear(void)
{
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    clear_cached_paths();
}

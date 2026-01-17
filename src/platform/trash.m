#import <Foundation/Foundation.h>
#include "trash.h"

bool platform_move_to_trash(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    @autoreleasepool {
        NSString *pathStr = [NSString stringWithUTF8String:path];
        if (pathStr == nil) {
            return false;
        }

        NSURL *fileURL = [NSURL fileURLWithPath:pathStr];
        if (fileURL == nil) {
            return false;
        }

        NSFileManager *fileManager = [NSFileManager defaultManager];
        NSError *error = nil;

        // Use trashItemAtURL which is available on macOS 10.8+
        BOOL success = [fileManager trashItemAtURL:fileURL
                                  resultingItemURL:nil
                                             error:&error];

        if (!success && error) {
            // Log error for debugging (optional)
            // NSLog(@"Failed to move to trash: %@", error.localizedDescription);
            return false;
        }

        return success;
    }
}

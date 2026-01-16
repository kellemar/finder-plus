#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "core/filesystem.h"

// Get current time in milliseconds
static double get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

int main(int argc, char *argv[])
{
    const char *test_path = "/tmp/finder_plus_perf_test";

    if (argc > 1) {
        test_path = argv[1];
    }

    printf("Performance Test: Directory Loading\n");
    printf("====================================\n");
    printf("Test path: %s\n\n", test_path);

    DirectoryState state;
    directory_state_init(&state);

    // Measure directory load time
    double start = get_time_ms();
    bool result = directory_read(&state, test_path);
    double end = get_time_ms();

    if (!result) {
        printf("ERROR: Failed to read directory: %s\n", state.error_message);
        return 1;
    }

    double load_time = end - start;

    printf("Results:\n");
    printf("  Files loaded: %d\n", state.count);
    printf("  Load time:    %.2f ms\n", load_time);
    printf("  Throughput:   %.0f files/sec\n", state.count / (load_time / 1000.0));

    // Check against target (500ms for 10K files)
    if (state.count >= 10000) {
        if (load_time < 500.0) {
            printf("\n  PASS: Load time under 500ms target\n");
        } else {
            printf("\n  WARNING: Load time exceeds 500ms target\n");
        }
    }

    // Test repeated reads (caching effects)
    printf("\nRepeated read test (5 iterations):\n");
    double total_time = 0;
    for (int i = 0; i < 5; i++) {
        start = get_time_ms();
        directory_read(&state, test_path);
        end = get_time_ms();
        double iter_time = end - start;
        printf("  Iteration %d: %.2f ms\n", i + 1, iter_time);
        total_time += iter_time;
    }
    printf("  Average: %.2f ms\n", total_time / 5.0);

    directory_state_free(&state);

    printf("\nPerformance test complete.\n");
    return 0;
}

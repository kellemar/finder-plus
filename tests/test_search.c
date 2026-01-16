#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Test helper functions
extern void inc_tests_run(void);
extern void inc_tests_passed(void);
extern void inc_tests_failed(void);

#define TEST_ASSERT(condition, message) do { \
    inc_tests_run(); \
    if (condition) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s (line %d)\n", message, __LINE__); \
    } \
} while(0)

#define TEST_ASSERT_EQ(expected, actual, message) do { \
    inc_tests_run(); \
    if ((expected) == (actual)) { \
        inc_tests_passed(); \
        printf("  PASS: %s\n", message); \
    } else { \
        inc_tests_failed(); \
        printf("  FAIL: %s - expected %d, got %d (line %d)\n", message, (int)(expected), (int)(actual), __LINE__); \
    } \
} while(0)

// Fuzzy match implementation for testing (copy of core logic)
#define SEARCH_MAX_MATCHES 64

static int to_lower(int c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

// Returns score (0 = no match, higher = better match)
static int search_fuzzy_match(const char *query, const char *text,
                               int *match_positions, int *match_count,
                               bool case_sensitive)
{
    if (!query || !text) return 0;
    if (query[0] == '\0') return 1; // Empty query matches everything

    const char *q = query;
    const char *t = text;
    int score = 0;
    int consecutive = 0;
    int matches = 0;
    bool prev_match = false;
    int pos = 0;

    while (*q && *t) {
        int q_char = case_sensitive ? *q : to_lower(*q);
        int t_char = case_sensitive ? *t : to_lower(*t);

        if (q_char == t_char) {
            if (match_positions && matches < SEARCH_MAX_MATCHES) {
                match_positions[matches] = pos;
            }
            matches++;

            // Bonus for consecutive matches (strong preference)
            if (prev_match) {
                consecutive++;
                score += 5 + consecutive * 2;
            } else {
                score += 1;
                consecutive = 0;
            }

            // Bonus for word start matches
            if (pos == 0 || t[-1] == ' ' || t[-1] == '/' ||
                t[-1] == '_' || t[-1] == '-' || t[-1] == '.') {
                score += 3;
            }

            prev_match = true;
            q++;
        } else {
            prev_match = false;
            consecutive = 0;
        }
        t++;
        pos++;
    }

    if (match_count) *match_count = matches;

    // Return 0 if not all query chars matched
    if (*q != '\0') return 0;

    return score;
}

void test_search(void)
{
    // Test basic exact match
    {
        int score = search_fuzzy_match("test", "test", NULL, NULL, false);
        TEST_ASSERT(score > 0, "Exact match should have positive score");
    }

    // Test case-insensitive match
    {
        int score = search_fuzzy_match("TEST", "test", NULL, NULL, false);
        TEST_ASSERT(score > 0, "Case-insensitive match should work");

        int score_sensitive = search_fuzzy_match("TEST", "test", NULL, NULL, true);
        TEST_ASSERT(score_sensitive == 0, "Case-sensitive should not match");
    }

    // Test partial match (fuzzy)
    {
        int score = search_fuzzy_match("mf", "myfile.txt", NULL, NULL, false);
        TEST_ASSERT(score > 0, "Fuzzy match 'mf' in 'myfile.txt' should work");
    }

    // Test consecutive bonus
    {
        int score_consec = search_fuzzy_match("abc", "abcdef", NULL, NULL, false);
        int score_sparse = search_fuzzy_match("abc", "a_b_c_d", NULL, NULL, false);
        TEST_ASSERT(score_consec > score_sparse, "Consecutive matches should score higher");
    }

    // Test word start bonus
    {
        int score_start = search_fuzzy_match("d", "dir", NULL, NULL, false);
        int score_middle = search_fuzzy_match("i", "dir", NULL, NULL, false);
        TEST_ASSERT(score_start > score_middle, "Word start match should score higher");
    }

    // Test no match
    {
        int score = search_fuzzy_match("xyz", "abcdef", NULL, NULL, false);
        TEST_ASSERT_EQ(0, score, "No match should return 0");
    }

    // Test match positions
    {
        int positions[SEARCH_MAX_MATCHES];
        int match_count = 0;
        int score = search_fuzzy_match("abc", "axbxcx", positions, &match_count, false);

        TEST_ASSERT(score > 0, "Match should succeed");
        TEST_ASSERT_EQ(3, match_count, "Should have 3 matches");
        TEST_ASSERT_EQ(0, positions[0], "First match at position 0");
        TEST_ASSERT_EQ(2, positions[1], "Second match at position 2");
        TEST_ASSERT_EQ(4, positions[2], "Third match at position 4");
    }

    // Test empty query
    {
        int score = search_fuzzy_match("", "anything", NULL, NULL, false);
        TEST_ASSERT_EQ(1, score, "Empty query should match with score 1");
    }

    // Test NULL inputs
    {
        int score = search_fuzzy_match(NULL, "test", NULL, NULL, false);
        TEST_ASSERT_EQ(0, score, "NULL query should return 0");

        score = search_fuzzy_match("test", NULL, NULL, NULL, false);
        TEST_ASSERT_EQ(0, score, "NULL text should return 0");
    }

    // Test partial query not fully matched
    {
        int score = search_fuzzy_match("abcd", "abc", NULL, NULL, false);
        TEST_ASSERT_EQ(0, score, "Query longer than match should return 0");
    }

    // Test path-style matching
    {
        int score = search_fuzzy_match("mf", "my/folder/file.txt", NULL, NULL, false);
        TEST_ASSERT(score > 0, "Should match across path separators");
    }

    // Test underscore word boundary
    {
        int score = search_fuzzy_match("tf", "test_file.txt", NULL, NULL, false);
        TEST_ASSERT(score > 0, "Should match with underscore word boundary");

        int positions[SEARCH_MAX_MATCHES];
        int match_count = 0;
        search_fuzzy_match("tf", "test_file.txt", positions, &match_count, false);
        TEST_ASSERT_EQ(2, match_count, "Should have 2 matches");
    }
}

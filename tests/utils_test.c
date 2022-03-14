// A simple test file used to test utils.c
//
//      Connor Shugg

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "../src/utils/utils.h"

// Helper function that tests the utility binary search function
static void test_binary_search()
{
    test_section("binary search");
    char* strings[] = {"a", "c", "d", "e", "f", "g", "h", "i", "j", "k"};

    // search for valid entries
    for (int i = 0; i < 10; i++)
    {
        char* key = strings[i];
        int result = array_search((void**) strings, 10, key, (int(*)(void*, void*)) strcmp);
        check(result == i, "search returned %d, not %d", result, i);
    }

    // search for invalid entries
    check(array_search((void**) strings, 10, "x", (int(*)(void*, void*)) strcmp) == -1,
          "search didn't return -1");
}

// Tests integer-to-byte and byte-to-integer conversions.
static void test_byte_conversions()
{
    test_section("uint32_t/byte conversions");
    uint8_t bytes[4];
    uint32_t test = 0xaabbccdd;
    u32_to_bytes(test, bytes);
    check(bytes[0] == 0xdd, "u32_to_bytes[0] = 0x%x, not 0xdd", bytes[0]);
    check(bytes[1] == 0xcc, "u32_to_bytes[1] = 0x%x, not 0xcc", bytes[1]);
    check(bytes[2] == 0xbb, "u32_to_bytes[2] = 0x%x, not 0xbb", bytes[2]);
    check(bytes[3] == 0xaa, "u32_to_bytes[3] = 0x%x, not 0xaa", bytes[3]);
    uint32_t test1 = bytes_to_u32(bytes);
    check(test1 == test, "bytes_to_u32 returned 0x%x, not 0x%x", test1, test);

    test_section("uint64_t/byte conversions");
    uint64_t test64 = 0x1122334455667788;
    uint8_t bytes64[8];
    u64_to_bytes(test64, bytes64);
    check(bytes64[0] == 0x88, "u64_to_bytes[0] = 0x%x, not 0x88", bytes64[0]);
    check(bytes64[1] == 0x77, "u64_to_bytes[1] = 0x%x, not 0x77", bytes64[1]);
    check(bytes64[2] == 0x66, "u64_to_bytes[2] = 0x%x, not 0x66", bytes64[2]);
    check(bytes64[3] == 0x55, "u64_to_bytes[3] = 0x%x, not 0x55", bytes64[3]);
    check(bytes64[4] == 0x44, "u64_to_bytes[4] = 0x%x, not 0x44", bytes64[4]);
    check(bytes64[5] == 0x33, "u64_to_bytes[5] = 0x%x, not 0x33", bytes64[5]);
    check(bytes64[6] == 0x22, "u64_to_bytes[6] = 0x%x, not 0x22", bytes64[6]);
    check(bytes64[7] == 0x11, "u64_to_bytes[7] = 0x%x, not 0x11", bytes64[7]);
    uint64_t test64_1 = bytes_to_u64(bytes64);
    check(test64_1 == test64, "bytes_to_u64 returned 0x%lx, not 0x%lx", test64_1, test64);
}

int main()
{
    test_section("mem alloc");
    // test allocating memory
    char* result = alloc_check(1);
    check(result != NULL, "alloc test 1");

    result = realloc_check(result, 4);
    check(result != NULL, "realloc test 1");
    free(result);

    test_section("string parsing");
    // test parsing strings
    char* s1 = "this_has_no_whitespace";
    char* s1r1 = strstr_whitespace(s1);
    char* s1r2 = strstr_non_whitespace(s1);
    char* s1_end = s1 + strlen(s1);
    char* s1r3 = strstr_whitespace_reverse(s1_end, strlen(s1));
    char* s1r4 = strstr_non_whitespace_reverse(s1 + 5, 5);
    check(s1r1 == NULL, "strstr_whitespace didn't return NULL");
    check(s1r2 == s1, "strstr_non_whitespace didn't return the same pointer");
    check(s1r3 == NULL, "strstr_whitespace_reverse didn't return NULL");
    check(s1r4 == s1 + 5, "strstr_non_whitespace_result failed");

    char* s2 = "this does have whitespace";
    char* s2r1 = strstr_whitespace(s2);
    char* s2r2 = strstr_non_whitespace(s2);
    char* s2_end = s2 + strlen(s2);
    char* s2r3 = strstr_whitespace_reverse(s2_end, strlen(s2));
    char* s2r4 = strstr_non_whitespace_reverse(s2_end, strlen(s2));
    check(s2r1 == s2 + 4, "strstr_whitespace didn't return the correct pointer");
    check(s2r2 == s2, "strstr_non_whitespace didn't return the correct pointer");
    check(s2r3 == s2 + 14, "strstr_whitespace_reverse didn't failed");
    check(s2r4 == s2 + strlen(s2), "strstr_non_whitespace_reverse failed");

    // run some other tests
    test_binary_search();
    test_byte_conversions();

    test_section("fatal error handling");
    // test a fatality
    printf("Allocating way too much memory - this should fail.\n");
    result = alloc_check(9999999999999);
    free(result); // <-- we should never reach here
    test_finish();
}

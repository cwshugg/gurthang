// Tests my buffer, defined in utils/buffer.h and implemented in utils/buffer.c.
//
//      Connor Shugg

#include <string.h>
#include "test.h"
#include "../src/utils/dict.h"

int main()
{
    test_section("dict from file");
    dict_t* d = dict_from_file("./dict.txt");
    check(d != NULL, "dict_from_file failed");
    printf("Sorted dictionary:\n");
    for (size_t i = 0; i < d->size; i++)
    {
        dict_entry_t* de = &d->entries[i];
        printf("  %lu. %s\n", i, de->str);
    }

    test_section("dict search");
    dict_entry_t* de = dict_search(d, "a");
    check(!strcmp(de->str, "a"), "failed to search for 'a'");
    de = dict_search(d, "ab");
    check(!strcmp(de->str, "ab"), "failed to search for 'ab'");
    de = dict_search(d, "abc");
    check(!strcmp(de->str, "abc"), "failed to search for 'abc'");
    de = dict_search(d, "abcdef");
    check(!strcmp(de->str, "abcdef"), "failed to search for 'abcdef'");

    test_section("dict random");
    for (int i = 0; i < 10; i++)
    {
        de = dict_get_rand(d);
        printf("RANDOM ENTRY: %s\n", de->str);
    }

    dict_free(d);
    free(d);

    test_finish();
    return 0;
}

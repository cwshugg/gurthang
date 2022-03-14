// Implements the dictionary functions defined in dict.h.
//
//      Connor Shugg

// Includes
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "utils.h"
#include "dict.h"

// =========================== Helper Functions ============================ //
// Attempts to allocate an internal string in the dictionary entry and copy the
// given string into it. Returns 0 on success and non-zero on failure.
static int dict_entry_init(dict_entry_t* de, char* str, size_t str_len)
{
    // if the string is empty or too big, return failure
    if (str_len == 0 || str_len > DICT_ENTRY_MAXLEN)
    { return -1; }
    // otherwise, allocate memory and copy the string in
    de->str = alloc_check((str_len * sizeof(char)) + 1);
    snprintf(de->str, str_len + 1, "%s", str);
    de->len = str_len;
    return 0;
}

// Frees internal memory for a given dictionary entry.
static void dict_entry_free(dict_entry_t* de)
{
    // free the internal string
    free(de->str);
}

// Comparison function used for sorting a list of dictionary entries. Calls
// strcmp() on the two entries' internal strings
static int dict_entry_cmp(const void* p1, const void* p2)
{
    dict_entry_t* de1 = (dict_entry_t*) p1;
    dict_entry_t* de2 = (dict_entry_t*) p2;
    return strcmp(de1->str, de2->str);
}

// Sorts a given dictionary's entries.
static void dict_sort(dict_t* dict)
{
    if (dict->size == 0)
    { return; }

    // invoke qsort
    qsort(dict->entries, dict->size, sizeof(dict_entry_t), dict_entry_cmp);
}


// ========================= Dictionary Interface ========================== //
dict_t* dict_from_file(char* fpath)
{
    FILE* fp = fopen(fpath, "r");
    if (!fp)
    { return NULL; }

    // allocate memory for a new dictionary
    dict_t* dict = alloc_check(sizeof(dict_t));
    dict_init(dict);

    ssize_t rcount = 0;
    char* line = NULL;
    size_t line_len = 0;
    // read line by line
    while ((rcount = getline(&line, &line_len, fp)) != -1)
    {
        // try to add it to the dictionary. On failure, free the dictionary and
        // the line string and return
        // One note: this function resorts the dictionary after EVERY add. This
        // is handy if you add something later on and it gets automatically
        // re-sorted for you. But right here, it's inefficient to invoke this
        // function. Considering this is a one-time init process though, I'll
        // keep it like this.
        line[rcount - 1] = '\0';
        if (dict_add(dict, line, rcount - 1))
        {
            dict_free(dict);
            free(line);
            fclose(fp);
            return NULL;
        }
    }

    // free excess memory and return
    free(line);
    fclose(fp);
    return dict;
}

void dict_init(dict_t* dict)
{
    dict->size = 0;
}

int dict_add(dict_t* dict, char* str, size_t str_len)
{
    // if the dictionary already contains this string, don't allow it
    if (dict_search(dict, str))
    { return -1; }

    // select the correct entry and attempt to add the string to it
    dict_entry_t* de = &dict->entries[dict->size];
    if (dict_entry_init(de, str, str_len))
    { return -1; }

    // increment the size, sort again, and return
    dict->size++;
    dict_sort(dict);
    return 0;
}

void dict_free(dict_t* dict)
{
    // iterate through the array of entries and free their memory
    for (size_t i = 0; i < dict->size; i++)
    { dict_entry_free(&dict->entries[i]); }
}

dict_entry_t* dict_search(dict_t* dict, char* word)
{
    // put together a stack-local dictionary entry so we can use our entry
    // comparison function 
    dict_entry_t local_de;
    local_de.str = word;
    local_de.len = strlen(word);
    
    // set up variables for binary search
    ssize_t left = 0;
    ssize_t right = dict->size - 1;
    ssize_t idx = -1;
    // iterate until the indexes cross (meaning we've failed the search)
    while (left <= right)
    {
        // compute middle index
        ssize_t mid = (left + right) / 2;
        int comparison = dict_entry_cmp(&local_de, &dict->entries[mid]);

        // adjust left/right accordingly
        if (comparison < 0)
        { right = mid - 1; }        // the key is less than the middle value
        else if (comparison > 0)
        { left = mid + 1; }         // the key is greater than the middle value
        else
        {                           // the key IS the middle value
            idx = mid;
            break;
        }
    }

    if (idx < 0)
    { return NULL; }
    return &dict->entries[idx];
}

dict_entry_t* dict_get_rand(dict_t* dict)
{
    if (dict->size == 0)
    { return NULL; }
    return &dict->entries[rand() % dict->size];
}

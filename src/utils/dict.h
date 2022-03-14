// This header files defines a word dictionary data structure. Simply put, it's
// a data structure that loads a text file of words into memory and supports
// various operations:
//  - Search the dictionary for an entry
//  - Get random entries from the dictionary
// I wrote this for the custom mutator to use.
//
// Dictionaries must be formatted to have one word per line. No blank lines are
// allowed.
//
//      Connor Shugg

#if !defined(DICT_H)
#define DICT_H

// Module includes
#include "list.h"

// Globals/defines
#define DICT_ENTRY_MAXLEN 128   // maximum length of one entry
#define DICT_MAXLEN 2048        // maximum number of entries in one dictionary

// ====================== Dictionary Data Structures ======================= //
// Represents a single dictionary entry.
typedef struct dict_entry
{
    char* str;              // the entry's string
    size_t len;             // length of the entry
} dict_entry_t;

// Represents one dictionary.
typedef struct dict
{
    dict_entry_t entries[DICT_MAXLEN]; // dictionary's entries (an array)
    size_t size;            // number of entries in the dictionary
    dllist_elem_t elem;     // used to store these in lists
} dict_t;


// ========================= Dictionary Interface ========================== //
// Takes in a file path and attempts to open it and parse all words into a
// heap-allocated dictionary. On success, a pointer to the dictionary is
// returned. On failure, NULL is returned.
dict_t* dict_from_file(char* fpath);

// Initializes the dictionary to have default values.
void dict_init(dict_t* dict);

// Given a null-terminated string, it's added to the dictionary, so long as the
// string doesn't exceed the maximum length and the dictionary has room.
// Returns 0 on success and a non-zero value on failure.
int dict_add(dict_t* dict, char* str, size_t str_len);

// Frees all memory within the given dictionary.
void dict_free(dict_t* dict);

// Searches the given dictionary for the given word. If it finds it, a pointer
// to the corresponding entry is returned. Otherwise, 
dict_entry_t* dict_search(dict_t* dict, char* word);

// Selects a random item from the dictionary and returns a pointer to it.
// Returns NULL if the dictionary is empty.
dict_entry_t* dict_get_rand(dict_t* dict);

#endif

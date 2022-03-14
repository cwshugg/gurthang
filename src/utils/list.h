// This module defines the structs and function prototypes for a simple
// doubly-linked list. Used throughout the fuzzer. This list implements two
// sentinel nodes at the front and rear.
//
//      Connor Shugg

#if !defined(LIST_H)
#define LIST_H

// Module inclusions
#include <inttypes.h>

// ========================== Struct Definitions =========================== //
// Typedefs for structs
typedef struct dllist_elem dllist_elem_t;
typedef struct dllist dllist_t;

// This struct definse a single member of the doubly-linked list.
struct dllist_elem
{
    dllist_elem_t* prev;      // previous list element in the chain
    dllist_elem_t* next;      // next list element in the chain
    void* container;        // pointer to the struct that contains this list elem
};

// Defines a doubly-linked list instance.
struct dllist
{
    dllist_elem_t head;      // pointer to the head sentinel node
    dllist_elem_t tail;      // pointer to the tail sentinel node
    uint32_t size;      // current size of the list
};


// ============================ List Interface ============================= //
// Takes in a list pointer and initializes the list struct.
void dllist_init(dllist_t* list);

// Returns the first non-sentinel node in the list, or NULL if it's empty.
dllist_elem_t* dllist_get_head(dllist_t* list);

// Returns the last non-sentinel node in the list, or NULL if it's empty.
dllist_elem_t* dllist_get_tail(dllist_t* list);

// Pushes a given list element to the FRONT of the given list.
// The 'container' is a pointer to the struct that contains 'elem'.
void dllist_push_head(dllist_t* list, dllist_elem_t* elem, void* container);

// Pushes a given list element to the BACK of the given list.
// The 'container' is a pointer to the struct that contains 'elem'.
void dllist_push_tail(dllist_t* list, dllist_elem_t* elem, void* container);

// Pops the HEAD entry off of the list. Returns NULL if empty.
dllist_elem_t* dllist_pop_head(dllist_t* list);

// Pops the TAIL entry off of the list. Returns NULL if empty.
dllist_elem_t* dllist_pop_tail(dllist_t* list);

// Takes a given list element and removes it from the list.
void dllist_remove(dllist_t* list, dllist_elem_t* elem);

// Simple for-loop iteration from head to tail.
#define dllist_iterate(list, elem) \
    for (elem = dllist_get_head(list); \
         ((dllist_elem_t*) elem) != NULL && ((dllist_elem_t*) elem) != &((dllist_t*) list)->tail; \
         elem = ((dllist_elem_t*) elem)->next)

// Simple for-loop iteration from head to tail that does NOT handle setting
// 'elem' to the next element on the list automatically. The caller must do
// this within the loop body. This is useful in certain situations, such as
// iterating through heap-allocated structs and freeing them as you go.
#define dllist_iterate_manual(list, elem) \
    for (elem = dllist_get_head(list); \
         ((dllist_elem_t*) elem) != NULL && ((dllist_elem_t*) elem) != &((dllist_t*) list)->tail; \
         /* DON'T advance to next elem. Caller must implement */)

#endif

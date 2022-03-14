// This implements the functions prototyped in list.h.
//
//      Connor Shugg

// Module inclusions
#include <stdlib.h>
#include <assert.h>
#include "list.h"

// Function prototypes
static void dllist_elem_remove(dllist_elem_t* elem);


// ============================ List Interface ============================= //
void dllist_init(dllist_t* list)
{
    list->head.prev = NULL;
    list->head.next = &list->tail;  // link sentinel head to tail
    list->tail.next = NULL;
    list->tail.prev = &list->head;  // link sentinel tail to head
    list->size = 0;
}

dllist_elem_t* dllist_get_head(dllist_t* list)
{
    assert(list);

    if (list->size > 0)
    { return list->head.next; }
    return NULL;
}

dllist_elem_t* dllist_get_tail(dllist_t* list)
{
    assert(list);

    if (list->size > 0)
    { return list->tail.prev; }
    return NULL;
}

void dllist_push_head(dllist_t* list, dllist_elem_t* elem, void* container)
{
    assert(list);
    assert(elem);
    elem->container = container;

    // adjust pointers and increment size
    elem->next = list->head.next;   // point new node at old first node
    elem->prev = &list->head;       // point new node at head sentinel node
    list->head.next->prev = elem;   // update old first node
    list->head.next = elem;         // update head sentinel's next pointer
    list->size++;
}

void dllist_push_tail(dllist_t* list, dllist_elem_t* elem, void* container)
{
    assert(list);
    assert(elem);
    elem->container = container;

    // adjust pointers and increment size
    elem->next = &list->tail;       // point new node at tail sentinel node
    elem->prev = list->tail.prev;   // point new node at old last node
    list->tail.prev->next = elem;   // update old last node
    list->tail.prev = elem;         // update tail sentinel's prev pointer
    list->size++;
}

dllist_elem_t* dllist_pop_head(dllist_t* list)
{
    assert(list);

    // get a pointer to the first entry (check for an empty list)
    dllist_elem_t* elem = dllist_get_head(list);
    if (!elem)
    { return NULL; }

    // get references to the previous and next entries and reset their pointers
    dllist_elem_remove(elem);
    list->size--;
    return elem;
}

dllist_elem_t* dllist_pop_tail(dllist_t* list)
{
    assert(list);

    // get a pointer to the last entry (check for an empty list)
    dllist_elem_t* elem = dllist_get_tail(list);
    if (!elem)
    { return NULL; }

    // get references to the previous and next entries and reset their pointers
    dllist_elem_remove(elem);
    list->size--;
    return elem;
}

void dllist_remove(dllist_t* list, dllist_elem_t* elem)
{
    assert(list && elem);

    // remove the list element, then decrement the list's size
    dllist_elem_remove(elem);
    list->size--;
}


// ============================= List Helpers ============================== //
// Takes a list elem (that's part of a list) and adjusts its previous and next
// node's pointers to effectively remove it from the list. The list elem's
// 'next' and 'prev' pointers will be zeroed out.
static void dllist_elem_remove(dllist_elem_t* elem)
{
    dllist_elem_t* prev = elem->prev;
    dllist_elem_t* next = elem->next;
    prev->next = next;  // adjust previous node
    next->prev = prev;  // adjust next node
    
    // to be safe, null-out the elem's pointers
    elem->prev = NULL;
    elem->next = NULL;
}

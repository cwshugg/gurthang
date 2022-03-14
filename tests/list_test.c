// Tests the doubly-linked list module.

// Module inclusions
#include "test.h"
#include "../src/utils/list.h"

// Sample struct for testing the list
typedef struct object
{
    int data;
    dllist_elem_t elem;
} Object;

// Main function
int main()
{
    test_section("list init");
    // initialize a new list
    dllist_t l;
    dllist_init(&l);

    // check initial conditions
    check(l.size == 0, "List initial size isn't 0.");
    check(!l.head.prev, "List initial head->prev isn't NULL.");
    check(l.head.next == &l.tail, "List initial head->next isn't tail.");
    check(!l.tail.next, "List initial tail->next isn't NULL.");
    check(l.tail.prev == &l.head, "List initial tail->prev isn't head.");


    test_section("list pushing");
    // create some new objects
    Object o1, o2, o3, o4;
    o1.data = 1;
    o2.data = 2;
    o3.data = 3;
    o4.data = 4;

    // push one and perform checks
    dllist_push_head(&l, &o1.elem, &o1);
    check(l.size == 1, "(1) List size wasn't updated.");
    // iterate and make sure we iterate only once
    dllist_elem_t* e;
    int count = 0;
    dllist_iterate(&l, e)
    {
        Object* o = e->container;
        check(o != NULL, "(1) Entry container is NULL.");
        check(o == &o1, "(1) Entry container didn't match.");
        check(o->data == 1, "(1) Entry's data wasn't 1.");
        count++;
    }
    check(count == 1, "(1) Did not iterate 1 time.");

    // push another and perform checks
    dllist_push_head(&l, &o2.elem, &o2);
    check(l.size == 2, "(2) List size wasn't updated.");
    // iterate
    count = 0;
    Object* expected_obj1[2] = {&o2, &o1};
    int expected_int1[2] = {2, 1};
    dllist_iterate(&l, e)
    {
        Object* o = e->container;
        check(o != NULL, "(2) Entry container is NULL.");
        check(o == expected_obj1[count], "(2) Entry %d container didn't match.",
              count);
        check(o->data == expected_int1[count], "(2) Entry %d data wasn't %d.",
              count, expected_int1[count]);
        count++;
    }
    check(count == 2, "(2) Did not iterate 2 times.");

    // push another and perform checks
    dllist_push_tail(&l, &o3.elem, &o3);
    check(l.size == 3, "(3) List size wasn't updated.");
    
    test_section("list iteration");
    // iterate
    count = 0;
    Object* expected_obj2[3] = {&o2, &o1, &o3};
    int expected_int2[3] = {2, 1, 3};
    dllist_iterate(&l, e)
    {
        Object* o = e->container;
        check(o != NULL, "(3) Entry container is NULL.");
        check(o == expected_obj2[count], "(3) Entry %d container didn't match.",
              count);
        check(o->data == expected_int2[count], "(3) Entry %d data wasn't %d.",
              count, expected_int2[count]);
        count++;
    }
    check(count == 3, "(3) Did not iterate 3 times.");

    Object o_extra;
    o_extra.data = 9999;
    dllist_push_tail(&l, &o_extra.elem, &o_extra);

    // attempt a manual iteration
    count = 0;
    dllist_iterate_manual(&l, e)
    {
        Object* o = e->container;
        e = e->next;
        count++;

        // remove the entry if the value matches
        if (o->data == 9999)
        { dllist_remove(&l, &o->elem); }
    }
    check(count == 4, "(3) Did not iterate 4 times (manual mode).");

    // remove some and check
    dllist_elem_t* e1 = dllist_pop_head(&l);
    check(e1 == &o2.elem, "Pop from head failed.");
    check(l.size == 2, "Pop from head didn't decrement size.");
    dllist_elem_t* e2 = dllist_pop_tail(&l);
    check(e2 == &o3.elem, "Pop from tail failed.");
    check(l.size == 1, "Pop from tail didn't decrement size.");

    // append a few more
    dllist_push_tail(&l, &o2.elem, &o2);
    dllist_push_head(&l, &o4.elem, &o4);
    dllist_push_head(&l, &o3.elem, &o3);
    check(l.size == 4, "List size wasn't updated properly");
    // iterate and check
    count = 0;
    Object* expected_obj3[4] = {&o3, &o4, &o1, &o2};
    int expected_int3[4] = {3, 4, 1, 2};
    dllist_iterate(&l, e)
    {
        Object* o = e->container;
        check(o != NULL, "(4) Entry container is NULL.");
        check(o == expected_obj3[count], "(4) Entry %d container didn't match.",
              count);
        check(o->data == expected_int3[count], "(4) Entry %d data wasn't %d.",
              count, expected_int3[count]);
        count++;
    }
    check(count == 4, "(4) Did not iterate 4 times.");
    
    test_section("list pop");
    // remove all
    e1 = dllist_pop_head(&l);
    check(e1 == &o3.elem, "Pop from head failed.");
    check(l.size == 3, "Pop from head didn't decrement size.");
    e1 = dllist_pop_tail(&l);
    check(e1 == &o2.elem, "Pop from tail failed.");
    check(l.size == 2, "Pop from tail didn't decrement size.");
    e1 = dllist_pop_tail(&l);
    check(e1 == &o1.elem, "Pop from tail failed.");
    check(l.size == 1, "Pop from tail didn't decrement size.");
    e1 = dllist_pop_head(&l);
    check(e1 == &o4.elem, "Pop from tail failed.");
    check(l.size == 0, "Pop from tail didn't decrement size.");

    // the list is empty - make sure we can't pop
    check(dllist_pop_head(&l) == NULL, "Empty list pop (head) returned something not NULL.");
    check(dllist_pop_tail(&l) == NULL, "Empty list pop (tail) returned something not NULL.");

    test_finish();
}

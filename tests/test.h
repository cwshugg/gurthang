// A small header file used to implement unit tests to make sure my code works.
//
//      Connor Shugg

#if !defined(TEST_H)
#define TEST_H

// Module inclusions
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>

// Globals/defines
#define C_NONE "\033[0m"    // no color
#define C_BAD "\033[31m"    // red
#define C_GOOD "\033[32m"   // green
#define C_ACCENT "\033[33m" // yellow

// Checks the given condition - if it's not true (0), the given failure message
// is printed to stderr and the program aborts.
void check(int condition, char* failure_message, ...)
{
    if (!condition)
    {
        // load up a buffer as a formatter string
        int buffsize = 1024;
        char buffer[buffsize + 1];
        snprintf(buffer, buffsize + 1, "%s!\nCheck Failure:%s %s\n",
                 C_BAD, C_NONE, failure_message);
        
        // print the message in a printf-like manner
        va_list vl;
        va_start(vl, failure_message);
        vfprintf(stdout, buffer, vl);
        va_end(vl);

        // assert to halt execution
        assert(condition);
    }
    else
    { fprintf(stdout, "%s.%s", C_GOOD, C_NONE); }
}

// A simple function used to print information for a section of tests. Made
// to make testing modules look a little prettier.
void test_section(char* format, ...)
{
    static int section_count = 0;

    // create a modified buffer to hold the format string, with some extras  
    int buffsize = 1024;
    char buffer[buffsize + 1];
    snprintf(buffer, buffsize + 1, "%s%sTest Section:%s %s\n",
             section_count == 0 ? "" : "\n", C_ACCENT, C_NONE, format); 
        
    // print the message in a printf-like manner to stdout
    va_list vl;
    va_start(vl, format);
    vfprintf(stdout, buffer, vl);
    va_end(vl);

    section_count++;
}

// Marks the end of a test module.
void test_finish()
{
    printf("%s\nTesting complete.%s\n", C_GOOD, C_NONE);
}

#endif

// Test module for fuzzing my HTTP parsing code.
//
//      Connor Shugg

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "test.h"
#include "../src/http/http.h"
#include "../src/utils/utils.h"

// Function used to fuzz http header parsing.
static void fuzz_header_parsing()
{
    http_header_t header;
    http_header_init(&header);

    // set up a buffer to read into
    size_t buff_size = 16384;
    size_t buff_usage = 0;
    char buff[buff_size];
    ssize_t amount_read = 0;

    // read until stdin is exhausted or our buffer fills up
    while (buff_usage < buff_size && (amount_read = read(STDIN_FILENO, buff + buff_usage, 1024)) > 0)
    { buff_usage += amount_read; }

    // check for read error
    if (amount_read == -1)
    { check(0, "Failed to read from stdin: %s", strerror(errno)); }

    // pass the buffer into the header parsing function
    http_parse_result_t res = http_header_parse(&header, buff);
    printf("Parse Result: %d\n", res);
    printf(" - Header Name:  \"%s\"\n", buffer_dptr(&header.name));
    printf(" - Header Value: \"%s\"\n", buffer_dptr(&header.value));

    // free memory, if applicable
    if (res == HTTP_PARSE_OK)
    { http_header_free(&header); }

    // run a check to make sure we know we got here
    check(1, "We didn't crash, so this check should pass.");
}

// Main function
int main()
{
    fuzz_header_parsing();
}


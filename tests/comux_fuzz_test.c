// Test module for fuzzing my comux file parsing code.
//
//      Connor Shugg

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "test.h"
#include "../src/comux/comux.h"
#include "../src/utils/utils.h"

// Main function
int main()
{
    test_section("manifest reading fuzz test");
    comux_manifest_t manifest;
    comux_manifest_init(&manifest);

    // pass stdin's descriptor for the reading function
    comux_parse_result_t res = comux_manifest_read(&manifest, STDIN_FILENO);
    printf("Parse result: %d\n", res);
    check(1, "If we got here, that means we didn't crash while reading");

    // write it out to a file
    int fd = open("./comux_fuzz_test.out", O_CREAT | O_WRONLY, 0644);
    check(fd != -1, "failed to open file descriptor");
    comux_manifest_write(&manifest, fd);
    check(1, "If we got here, that means writing didn't crash");
    close(fd);

    // free memory and return
    comux_manifest_free(&manifest, 1);
    test_finish();
}


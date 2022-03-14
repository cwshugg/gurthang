// Tests the logging module.

// Module inclusions
#include <string.h>
#include <unistd.h>
#include "test.h"
#include "../src/utils/log.h"

// Main function
int main()
{
    char* envar = "LOG_TEST";
    printf("Set this environment variable to test: %s\n", envar);

    test_section("log init");
    log_t l1;
    log_init(&l1, "test-log", envar);
    check(l1.time_init > 0, "time wasn't initialized");
    check(!strcmp(l1.prefix, "test-log"), "prefix was set to '%s' instead of '%s'",
          l1.prefix, "test-log");

    test_section("log write");
    log_write(&l1, "testing1");
    log_write(&l1, "testing2");
    log_write(&l1, "testing3");
    log_write(&l1, "testing4");
    sleep(1);
    log_write(&l1, "testing5");
    sleep(1);
    log_write(&l1, "testing6");
    sleep(1);
    log_write(&l1, "testing7");
    sleep(1);
    log_write(&l1, "testing8");

    test_section("log deinit");
    log_free(&l1);
    check(l1.fpath == NULL, "file path string wasn't de-initialized");

    test_finish();
}

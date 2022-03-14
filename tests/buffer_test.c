// Tests my buffer, defined in utils/buffer.h and implemented in utils/buffer.c.
//
//      Connor Shugg

#include <string.h>
#include "test.h"
#include "../src/utils/buffer.h"

int main()
{
    test_section("buffer init");
    // initialize a buffer then perform checks
    buffer_t buff;
    buffer_init(&buff, 0);
    buffer_free(&buff);
    check(buff.data == NULL, "buffer data isn't NULL");
    check(buff.cap == 0, "buffer cap isn't 0");
    buffer_init(&buff, 16);
    check(buff.cap == 16, "buffer cap isn't 16");
    check(buff.size == 0, "buffer size isn't 0");
    check(buff.data != NULL, "buffer data ptr is NULL");

    test_section("buffer append");
    char* data = "123456789a";
    // append once
    check(buffer_append(&buff, data) == 10, "buffer_append didn't return 10");
    check(!strcmp(buff.data, data), "buffer_append didn't append the correct bytes");
    check(buff.size == 10, "buffer_append didn't set size correctly");
    check(buff.cap == 16, "buffer_append grew the heap space when it shouldn't have");
    check(*(buff.data + buff.size) == '\0', "buffer_append didn't add a terminator");
    // append twice (the buffer should grow)
    check(buffer_append(&buff, data) == 10, "buffer_append didn't return 10");
    check(!strcmp(buff.data, "123456789a123456789a"),
          "buffer_append didn't append the correct bytes");
    check(buff.size == 20, "buffer_append didn't set size correctly");
    check(buff.cap == 32 + 11, "buffer_append didn't adjust the capacity correctly");
    check(*(buff.data + buff.size) == '\0', "buffer_append didn't add a terminator");
    // append thrice (the buffer should NOT grow)
    check(buffer_append(&buff, data) == 10, "buffer_append didn't return 10");
    check(!strcmp(buff.data, "123456789a123456789a123456789a"),
          "buffer_append didn't append the correct bytes");
    check(buff.size == 30, "buffer_append didn't set size correctly");
    check(buff.cap == 32 + 11, "buffer_append adjusted the capacity when it shouldn't have");
    check(*(buff.data + buff.size) == '\0', "buffer_append didn't add a terminator");

    // free and reinitialize
    buffer_free(&buff);
    buffer_init(&buff, 16);
    test_section("buffer appendn");

    char* data2 = "0123456789abcdef";
    check(buffer_appendn(&buff, data2, 4) == 4, "buffer_appendn didn't return 4");
    check(!strcmp(buff.data, "0123"), "buffer_appendn didn't append the correct bytes");
    check(buff.size == 4, "buffer_appendn didn't set the size correctly");
    check(buff.cap == 16, "buffer_appendn changed the cap when it shouldn't have");
    check(*(buff.data + buff.size) == '\0', "buffer_appendn didn't add a terminator");

    // free and reinitialize
    buffer_free(&buff);
    buffer_init(&buff, 18);
    test_section("buffer appendf");

    char* format = "n: %d";
    // append with a 3-digit number (shouldn't resize)
    check(buffer_appendf(&buff, format, 123) == 6, "buffer_appendf didn't return 6");
    check(!strcmp(buff.data, "n: 123"), "buffer_appendf didn't append the correct bytes");
    check(buff.size == 6, "buffer_appendf didn't set the size correctly");
    check(buff.cap == 18, "buffer_appendf changed the cap when it shouldn't have");
    check(*(buff.data + buff.size) == '\0', "buffer_append didn't add a terminator");
    // append with a 10-digit number (SHOULD resize)
    check(buffer_appendf(&buff, format, 1234567890) == 13, "buffer_appendf didn't return 13");
    check(!strcmp(buff.data, "n: 123n: 1234567890"), "buffer_appendf didn't append the correct bytes");
    check(buff.size == 19, "buffer_appendf didn't set the size correctly");
    check(buff.cap == 50, "buffer_appendf didn't adjust the capacity correctly");
    check(*(buff.data + buff.size) == '\0', "buffer_append didn't add a terminator");

    // free memory
    buffer_free(&buff);

    test_finish();
}

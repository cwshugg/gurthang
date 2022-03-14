// Implements the function prototypes from utils.h.
//
//      Connor Shugg

// Module inclusions
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "utils.h"

// Whitespace array
static const char whitespace[6] = {' ', '\t', '\n', '\v', '\f', '\r'};

// ============================ Error Utilities ============================ //
static uint8_t use_internal_exit = 0;

void fatality(char* format, ...)
{
    // create a modified formatter string with a prefix
    size_t length = strlen(format) + 32;
    char modified_format[length];
    snprintf(modified_format, length, C_ERR "Fatal Error: " C_NONE "%s\n", format);

    // print the format to stderr
    va_list vl;
    va_start(vl, format);
    vfprintf(stdout, modified_format, vl);
    va_end(vl);

    // exit in one of two ways
    if (use_internal_exit)
    { _exit(FATAL_EXIT_CODE); }
    else
    { exit(FATAL_EXIT_CODE); }
}

void fatality_errno(int err, char* format, ...)
{
    // create a modified formatter string with a prefix
    size_t length = strlen(format) + 128;
    char modified_format[length];
    snprintf(modified_format, length, C_ERR "Fatal Error: " C_NONE "%s (%s)\n",
             format, strerror(err));

    // print the format to stderr
    va_list vl;
    va_start(vl, format);
    vfprintf(stdout, modified_format, vl);
    va_end(vl);

    // exit in one of two ways
    if (use_internal_exit)
    { _exit(FATAL_EXIT_CODE); }
    else
    { exit(FATAL_EXIT_CODE); }
}

void fatality_set_exit_method(uint8_t use_internal)
{ use_internal_exit = use_internal != 0; }

// =========================== String Utilities ============================ //
uint8_t char_is_whitespace(char c)
{
    // iterate through all whitespace characters and return '1' if there's a
    // match. Otherwise, return 0
    for (int i = 0; i < sizeof(whitespace); i++)
    {
        if (c == whitespace[i])
        { return 1; }
    }
    return 0;
}

uint8_t char_is_non_whitespace(char c)
{ return !char_is_whitespace(c); }

// Helper function for the 'strstr_whitespace' and 'strstr_non_whitespace'
// functions.
static char* strstr_whitespace_helper(char* source, uint8_t (cmp_func)(char))
{
    char* result = NULL;
    char* current = source;
    // iterate until we reach the max OR whitespace is found OR we reach a
    // terminator byte ('\0')
    while (!result && *current != '\0')
    {
        // compare the current character with each whitespace character, and
        // update the result pointer if there's a match
        result = cmp_func(*current) ? current : result;
        current++;
    }
    return result;
}

char* strstr_whitespace(char* source)
{ return strstr_whitespace_helper(source, char_is_whitespace); }

char* strstr_non_whitespace(char* source)
{ return strstr_whitespace_helper(source, char_is_non_whitespace); }


char* strstr_whitespace_reverse_helper(char* source_end, size_t source_len,
                                       uint8_t (cmp_func)(char))
{
    char* result = NULL;
    char* current = source_end;
    // iterate until we've found something or we've reached the beginning of
    // the string
    while (!result && source_len > 0)
    {
        result = cmp_func(*current) ? current : result;
        source_len--;
        current--;
    }
    return result;
}

char* strstr_whitespace_reverse(char* source_end, size_t source_len)
{ return strstr_whitespace_reverse_helper(source_end, source_len, char_is_whitespace); }

char* strstr_non_whitespace_reverse(char* source_end, size_t source_len)
{ return strstr_whitespace_reverse_helper(source_end, source_len, char_is_non_whitespace); }


// ======================== Search & Sort Utilities ======================== //
int qsort_u32_cmp(const void* a, const void* b)
{ return *((uint32_t*) a) - *((uint32_t*) b); }


// ====================== Integer/Bit/Byte Utilities ======================= //
// Helper macro for converting an integer to an array of bytes.
#define INT_TO_BYTES_HELPER(val, bits, array)               \
    do                                                      \
    {                                                       \
        for (uint8_t shift = 0; shift < bits; shift += 8)   \
        { array[shift / 8] = (val >> shift) & 0xff; }       \
    } while (0)                                             \

// Helper macro for converting bytes to an integer.
#define BYTES_TO_INT_HELPER(retval, bits, type, array)      \
    do                                                      \
    {                                                       \
        *((type*) retval) = 0;                              \
        for (uint8_t shift = 0; shift < bits; shift += 8)   \
        { *((type*) retval) |= (type) (array[shift / 8]) << (type) shift; } \
    } while (0);                                            \
    

void u32_to_bytes(uint32_t val, uint8_t* dest)
{ INT_TO_BYTES_HELPER(val, 32, dest); }

uint32_t bytes_to_u32(uint8_t* src)
{
    uint32_t result = 0;
    BYTES_TO_INT_HELPER(&result, 32, uint32_t, src);
    return result;
}

void u64_to_bytes(uint64_t val, uint8_t* dest)
{ INT_TO_BYTES_HELPER(val, 64, dest); }

uint64_t bytes_to_u64(uint8_t* src)
{
    uint64_t result = 0;
    BYTES_TO_INT_HELPER(&result, 64, uint64_t, src);
    return result;
}

int str_to_int(char* value, long int* retval)
{
    // invoke strtol to attempt a conversion
    char* endptr;
    long int result = strtol(value, &endptr, 10);

    // if we failed, return a non-zero
    if (result == 0 && endptr == value)
    { return 1; }

    // otherwise, update retval and return 0
    *retval = result;
    return 0;
}


// ========================== File I/O Utilities =========================== //
ssize_t read_check(int fd, void* buf, size_t count)
{
    // attempt to read, then check for error
    ssize_t amount_read = read(fd, buf, count);
    if (amount_read == -1)
    { fatality_errno(errno, "failed to read bytes from file descriptor %d.", fd); }

    return amount_read;
}

ssize_t write_check(int fd, const void* buf, size_t count)
{
    // attempt to write, then check for error, or not enough bytes
    ssize_t amount_written = write(fd, buf, count);
    if (amount_written == -1)
    { fatality_errno(errno, "failed to write bytes to file descriptor %d.", fd); }
    else if (amount_written < count)
    {
        fatality("couldn't write all %ld bytes (only wrote %ld) to file descriptor %d.",
                 count, amount_written, fd);
    }

    return amount_written;
}


// =========================== Memory Utilities ============================ //
void* alloc_check(size_t bytes)
{
    void* result = malloc(bytes);
    if (!result)
    { fatality("failed to allocate %ld bytes", bytes); }
    return result;
}

void* realloc_check(void* ptr, size_t bytes)
{
    void* result = realloc(ptr, bytes);
    if (!result)
    { fatality("failed to reallocate %p to %ld bytes", ptr, bytes); }
    return result;
}

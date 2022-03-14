// This header file defines various utility functions used by this project.
//
//      Connor Shugg

#if !defined(UTILS_H)
#define UTILS_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>

// Defines
#define C_NONE "\033[0m"
#define C_ERR "\033[31m"
#define FATAL_EXIT_CODE 24060

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// Space-tabs - used for string formatting
#define STAB "    "
#define STAB_TREE1 " \u2514\u2500 "
#define STAB_TREE2 " \u251c\u2500 "
#define STAB_TREE3 " \u2503  "

// ============================ Error Utilities ============================ //
// Used when a fatal error is encountered and the program needs to exit. Prints
// the given message (format string & args) to stderr, then exits.
void fatality(char* format, ...);

// Used when a fatal error is encountered and errno was set. This behaves the
// same as fatality(), but also adds 'strerror(err)' to the output.
void fatality_errno(int err, char* format, ...);

// Used to tell the utility module which function to call when exiting on a
// fatal error. By default exit() is used, but if a non-zero value is passed
// into this function, _exit() will be used instead.
void fatality_set_exit_method(uint8_t use_internal);


// =========================== String Utilities ============================ //
// Helper used for functions that take in variable-length arguments that calls
// snprintf with the following given parameters:
//      dest        The destination char*
//      max_len     The maximum length to the copied into 'dest'
//      len         A ssize_t* used to save the length of the string
//      format      The format string used for vsnprintf.
#define VSNPRINTF_HELPER(dest, max_len, len, format) do \
{ \
    va_list vl; \
    va_start(vl, (char*) format); \
    *((ssize_t*) len) = vsnprintf((char*) dest, (size_t) max_len, \
                                  (char*) format, vl); \
    va_end(vl); \
} while (0);

// Returns 1 if the given character is considered whitespace. 0 otherwise.
uint8_t char_is_whitespace(char c);

// Returns 1 if the given character is NOT whitespace. 1 otherwise.
uint8_t char_is_non_whitespace(char c);

// Takes the given string and searches for the first whitespace character it
// can find. Returns NULL if none was found before a terminator, or a pointer
// to the first whitespace it found.
char* strstr_whitespace(char* source);

// Takes the given string and searches for the first non-whitespace character.
// Returns a pointer to the character if found, and NULL otherwise.
char* strstr_non_whitespace(char* source);

// Takes a pointer to the end of a string, and the string's length, and walks
// backwards to the beginning of the string until a whitespace character is
// found. If none is found, NULL is returned. Otherwise, a pointer to the
// whitespace character is returned.
char* strstr_whitespace_reverse(char* source_end, size_t source_len);

// Takes a pointer to the end of a string, and the string's length, and walks
// backwards to the beginning of the string until a non-whitespace character is
// found. If none is found, NULL is returned. Otherwise, a pointer to the
// non-whitespace character is returned.
char* strstr_non_whitespace_reverse(char* source, size_t source_len);


// ======================== Search & Sort Utilities ======================== //
// Used to sort an array of uint32_t integers.
int qsort_u32_cmp(const void* a, const void* b);


// ====================== Integer/Bit/Byte Utilities ======================= //
// Takes in a 32-bit unsinged integer and a pointer to memory with at least 4
// 8-bit integers available and uses bitwise operations to split it into bytes.
// The least-significant byte is placed in the first slot, and the most
// significant byte is placed in the last slot.
void u32_to_bytes(uint32_t val, uint8_t* dest);

// Takes a pointer to 4 8-bit integers (bytes) and uses them to reconstruct a
// single 32-bit integer using bitwise operations. It assumes the first byte is
// the least-significant byte, and the last is the most-significant byte.
uint32_t bytes_to_u32(uint8_t* src);

// Performs the same function as 'u32_to_bytes', but instead expects a 64-bit
// integer and produces 8 bytes.
void u64_to_bytes(uint64_t val, uint8_t* dest);

// Performs the same function as 'bytes_to_u64', but instead expects 8 bytes
// and produces a 64-bit number.
uint64_t bytes_to_u64(uint8_t* src);

// Takes in a string and attempts to convert it to an integer. Returns 0 on
// success and a non-zero value on error. On success, *retval is updated to
// hold the converted integer.
int str_to_int(char* value, long int* retval);


// ========================== File I/O Utilities =========================== //
// A wrapper for the 'read' system call that checks the return value and throws
// an error on failure to read.
// Useful in some cases where a 'read' is expected to complete without issue.
ssize_t read_check(int fd, void* buf, size_t count);

// A wrapper for the 'write' system call that checks the return value and
// throws an error on failure to write (or if not enough bytes were written).
// Useful in some cases where a 'write' is expected to complete without issue.
ssize_t write_check(int fd, const void* buf, size_t count);


// =========================== Memory Utilities ============================ //
// A simple malloc() wrapper that checks the return value. On failure to alloc,
// an error is thrown and the program exits.
void* alloc_check(size_t bytes);

// Wrapper for realloc() that checks the return value and throws an error on
// failure.
void* realloc_check(void* ptr, size_t bytes);

#endif

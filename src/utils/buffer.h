// This header file defines a simple resizeable buffer that uses heap memory to
// store bytes. Effectively, it's supposed to serve as a fancy string.
// The buffer will automatically resize itself as needed, and will always place
// a null terminator byte ('\0') at the end of the buffer.
//
//      Connor Shugg

#if !defined(BUFFER_H)
#define BUFFER_H

// Module inclusions
#include <stdlib.h>

// The main buffer struct.
typedef struct buffer
{
    char* data;         // pointer to the buffer's chunk of heap memory
    size_t size;        // number of bytes currently filled in the buffer
    size_t cap;         // the capacity of the buffer (the allocated size)
} buffer_t;

// A quick way to convert a buffer_t* pointer into its inner char* data ptr.
#define buffer_dptr(buff) (((buffer_t*) buff)->data)
// A quick way to get the "next pointer" for a buffer - that is, the point to
// the next free spot in its memory.
#define buffer_nptr(buff) (((buffer_t*) buff)->data + ((buffer_t*) buff)->size)
// A quick way to extract the buffer's size from a buffer_t*.
#define buffer_size(buff) (((buffer_t*) buff)->size)
// Macro to manually increase the buffer's size. Useful if you wrote something
// to the buffer in a way that didn't involve calling buffer_append* functions.
#define buffer_size_increase(buff, increase) (((buffer_t*) buff)->size += (size_t) increase)

// Initializes the given buffer with an initial capacity, setting the size to
// zero and the other fields appropriately.
void buffer_init(buffer_t* buff, size_t capacity);

// Takes a string and attempts to append it to the buffer.
// Returns the number of bytes that were written.
size_t buffer_append(buffer_t* buff, char* string);

// Works the same as buffer_append, but instead appends at most 'n' bytes to
// the buffer from the given string.
size_t buffer_appendn(buffer_t* buff, char* string, size_t n);

// A printf-like function that appends the given format string and its contents
// to the end of the buffer.
// Returns the number of bytes that were written.
size_t buffer_appendf(buffer_t* buff, char* format, ...);

// Takes a given buffer and frees the memory allocated for it. The struct
// fields are reset to default values.
void buffer_free(buffer_t* buff);

// Used to effectively mark the data within the buffer as "cleared" or
// "invalidated". This slides the internal char* pointer back to the front of
// the buffer's memory block, allowing for the same bytes to be reused.
void buffer_reset(buffer_t* buff);

#endif

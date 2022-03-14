// Implements the functions from buffer.h.
//
//      Connor Shugg

#include <string.h>
#include <stdarg.h>
#include "buffer.h"
#include "utils.h"

// ======================= Helper Functions & Macros ======================= //
// Helper function that ensures the buffer has at least 'bytes_needed' room of
// unused bytes.
static void buffer_capacity_check(buffer_t* buff, size_t bytes_needed)
{
    // if there's already enuough room, just return
    if (bytes_needed <= buff->cap - buff->size)
    { return; }

    // otherwise, attempt to reallocate
    size_t new_size = (buff->cap * 2) + bytes_needed;
    buff->data = realloc_check(buff->data, new_size * sizeof(char));
    buff->cap = new_size;
}


// ========================== Main Functionality =========================== //
void buffer_init(buffer_t* buff, size_t capacity)
{
    // allocate memory, if applicable
    if (capacity > 0)
    { buff->data = alloc_check(capacity * sizeof(char)); }
    else
    { buff->data = NULL; }

    // set the other fields accordingly
    buff->cap = capacity;
    buff->size = 0;
}

size_t buffer_append(buffer_t* buff, char* string)
{ return buffer_appendn(buff, string, strlen(string)); }

size_t buffer_appendn(buffer_t* buff, char* string, size_t n)
{
    // ensure we have the capacity for the right number of bytes
    buffer_capacity_check(buff, n + 1); // +1 for '\0'

    // write into the buffer, adjust the size, and return
    memcpy(buffer_nptr(buff), string, n);
    buff->size += n;
    *buffer_nptr(buff) = '\0';    
    return n;
}

size_t buffer_appendf(buffer_t* buff, char* format, ...)
{
    // make an educated guess at how much space we'll need and ensure the
    // buffer can fit it
    buffer_capacity_check(buff, (strlen(format) * 2) + 1); // +1 for '\0'

    // attempt to write to the buffer
    ssize_t bytes_available = 0;
    ssize_t bytes_written = 0;
    do
    {
        // update the total number of bytes available, 
        bytes_available = buff->cap - buff->size;
        VSNPRINTF_HELPER(buffer_nptr(buff), bytes_available, &bytes_written, format);

        // if needed, resize
        if (bytes_written > bytes_available)
        { buffer_capacity_check(buff, bytes_written + 1); } // +1 for '\0'
    } while (bytes_written > bytes_available);

    // adjust the size, add a terminator, and return the number of bytes written
    buff->size += bytes_written;
    return bytes_written;
}

void buffer_free(buffer_t* buff)
{
    // free the heap space
    if (buff->data)
    { free(buff->data); }

    // reset struct fields
    buff->data = NULL;
    buff->size = 0;
    buff->cap = 0;
}

void buffer_reset(buffer_t* buff)
{
    // reset the fields needed to indicate the buffer is reusable
    buff->size = 0;
}

// This header file defines the main interface useable by other C program to
// build comux files. A "comux file" is a specially-formatted file that defines
// specific content to be sent to a web server over multiple connections.
//
// Comux File Layout
// =================
// The beginning of a comux file (called the 'header') is formatted like this:
// +---------------------------------------------------+
// | MAGIC      VERSION      NUM_CONNS      NUM_CHUNKS |
// +---------------------------------------------------+
// Where:
//  - MAGIC is an 8-byte identifier: "comux\0\0\0" ("comux", then 3 '\0's)
//  - VERSION is a 4-byte integer specifying the comux version number.
//    (the original version being 0. I added this for future extensibility.)
//  - NUM_CONNS is a 4-byte integer specifying the number of connections
//    to be created concurrently with the target server.
//  - NUM_CHUNKS is a 4-byte integer specifying the number of chunks defined
//    in the file.
//
// Immediately following the header is the first chunk header.
// The chunk header describes a specific chunk defined in the file, and is
// immediately followed by the chunk's data itself. Each chunk's data is to
// be sent across the wire to one of the NUM_CONNS connections with the target
// web server.
// The header is formatted like so:
// +----------------------------------------+
// | CONN_ID CONN_LEN CONN_SCHED CONN_FLAGS |
// +----------------------------------------+
// Where:
//  - CONN_ID is a 4-byte integer containing a unique ID number.
//  - CONN_LEN is a 8-byte integer representing the number of bytes in this
//    chunk's data block.
//  - CONN_SCHED is a 4-byte integer specifying a scheduling order for the
//    chunk. This is a way for the recipient and parser of a comux file to
//    determine which chunks to send when.
//  - CONN_FLAGS is a 4-byte bit field allowing a series of flags to be
//    specified about the chunk. This is used to tell a parser of a comux
//    file *how* to treat this the sending of this chunk across connection..
//
// Chunk Scheduling
// ---------------------
// Chunk schedule values are unsigned integers, where chunks with a
// lower scheduling number are to be sent before those with a higher number.
// So, for example, if a comux file specifies two connections, and has the
// following chunks:
//      Chunk Number    Chunk Connection ID    Chunk Scheduling
//      -------------------------------------------------------
//          0                   0                     8
//          1                   1                     2
//          2                   1                     4
//          3                   0                     1
// The parser of this file will understand that:
//  - Chunk 3 is sent first, and is sent to connection 0.
//  - Chunk 1 is sent next, and is sent to connection 1.
//  - Chunk 2 is sent next, and is sent to connection 1.
//  - Chunk 0 is sent last, and is sent to connection 0.
//
//      Connor Shugg

#if !defined(COMUX_H)
#define COMUX_H

// Module inclusions
#include <inttypes.h>
#include <sys/types.h>
#include "../utils/list.h"
#include "../utils/buffer.h"

// ============================= Comux Parsing ============================= //
// An enum defining various possible results from parsing a comux file.
typedef enum comux_parse_result
{
    COMUX_PARSE_OK = 0,             // parsing succeeded
    COMUX_PARSE_EOF = -1,           // reached end of file

    COMUX_PARSE_BAD_MAGIC = 1,      // 'magic' bytes in the header were wrong
    COMUX_PARSE_BAD_VERSION = 2,    // not enough bytes to parse version
    COMUX_PARSE_BAD_NUM_CONNS = 3,  // not enough bytes to parse num_conns
    COMUX_PARSE_BAD_NUM_CHUNKS = 4, // not enough bytes to parse num_chunks
    COMUX_PARSE_BAD_CONN_ID = 5,    // not enough bytes to read cinfo ID
    COMUX_PARSE_BAD_CONN_LEN = 6,   // not enough bytes to read cinfo length
    COMUX_PARSE_BAD_CONN_SCHED = 7, // not enough bytes to read cinfo sched val
    COMUX_PARSE_BAD_CONN_FLAGS = 8, // not enough bytes to read cinfo flag bits
    COMUX_PARSE_CONN_LEN_MISMATCH = 9, // not enough bytes in the cinfo's data
} comux_parse_result_t;

// Takes in a comux parse result and returns a string describing the issue.
char* comux_parse_result_string(comux_parse_result_t result);


// =========================== The Comux Header ============================ //
#define COMUX_MAGIC_LEN 8
#define COMUX_MAGIC "comux!!!"

// This struct defines the members of the comux header struct. The header is
// written to the very beginning of the file, and is parsed to understand the
// file's contents.
typedef struct comux_header
{
    char magic[COMUX_MAGIC_LEN]; // header's ID string
    uint32_t version;           // 32-bit version number
    uint32_t num_conns;         // 32-bit number of connections to create
    uint32_t num_chunks;        // 32-bit number of chunks in the file
} comux_header_t;

// Takes the given comux header and initializes it to hold default values.
void comux_header_init(comux_header_t* header);

// Takes a comux header struct and frees any fields within.
void comux_header_free(comux_header_t* header);

// Takes in a header struct and a file descriptor opened for writing and writes
// the header's contents out into the file. Returns the number of bytes
// written.
size_t comux_header_write(comux_header_t* header, int fd);

// Performs the same action as 'comux_header_write', but instead writes into a
// given buffer of maximum length 'max_len'.
// This function assumes 'buff' does not overlap with the header's memory.
// Returns the number of bytes written, or a negative number indicating how
// much room was needed in total (when not enough room is available).
ssize_t comux_header_write_buffer(comux_header_t* header, char* buff,
                                 size_t max_len);

// Takes in a comux header pointer and a pointer to an already-opened file for
// reading. This function assumes the fd's cursor is at the right spot, and it
// attempts to read the header from the next few bytes.
comux_parse_result_t comux_header_read(comux_header_t* header, int fd);

// Performs the same action as 'comux_header_read', but instead reads from a
// given buffer, rather than an open file descriptor.
// The 'read_len' pointer is used to return the number of bytes read from
// the given buffer. (It's only filled on a successful read)
comux_parse_result_t comux_header_read_buffer(comux_header_t* header,
                                              char* buff, size_t buff_len,
                                              size_t* read_len);


// ======================== The Comux Chunk Header ========================= //
#define COMUX_CHUNK_DATA_MAXLEN 524288

// This enum defines a series of flags used for the 'flags' field in the cinfo
// struct.
typedef enum comux_chunk_flags
{
    COMUX_CHUNK_FLAGS_NONE = 0x0,           // no flags
    COMUX_CHUNK_FLAGS_AWAIT_RESPONSE = 0x1, // wait for the server's response
    COMUX_CHUNK_FLAGS_NO_SHUTDOWN = 0x2,    // DON'T shutdown() socket write-end
    // -------------------------------
    COMUX_CHUNK_FLAGS_ALL = 0x3             // ALL current flags
} comux_chunk_flags_t;

// This struct defines all the information about a single chunk defined in
// a comux file. A chunk represents a literal chunk of data to be sent to the
// target server, with some extra metadata (such as *which* connection to send
// it across, *when* to send it, etc.)
typedef struct comux_chunk_info
{
    uint32_t id;                // specifies which connection the data goes to
    uint64_t len;               // the chunk's data's length
    uint32_t sched;             // the chunk's scheduling value
    uint32_t flags;             // the chunk's flag bitfield

    buffer_t data;              // a buffer used to store the chunk data
    off_t offset;               // offset field set when reading from a file

    dllist_elem_t elem;           // list element
} comux_cinfo_t;

// Initializes the struct to hold default values.
void comux_cinfo_init(comux_cinfo_t* cinfo);

// Takes a chunk info pointer and frees any memory pointers within.
void comux_cinfo_free(comux_cinfo_t* cinfo);

// Takes in a cinfo struct and a file descriptor opened for writing and writes
// the header's contents out into the file. Returns the number of bytes
// written.
size_t comux_cinfo_write(comux_cinfo_t* cinfo, int fd);

// Performs the same action as 'comux_cinfo_write', but on a given buffer with
// a maximum length of 'max_len'.
// This function assumes 'buff' does not overlap with the cinfo's memory.
// Returns the number of bytes written, or a negative number indicating how
// much room was needed in total (when not enough room is available).
// NOTE: the cinfo's 'offset' field is NOT set in this function.
ssize_t comux_cinfo_write_buffer(comux_cinfo_t* cinfo, char* buff,
                                 size_t max_len);

// Takes in a cinfo struct pointer and an already-opened file descriptor for
// reading, then attempts to parse the next few bytes and store their values
// into the comux cinfo header struct.
comux_parse_result_t comux_cinfo_read(comux_cinfo_t* cinfo, int fd);

// Performs the same action as 'comux_cinfo_read', but instead reads from a
// buffer of max length 'buff_len'.
// The 'read_len' pointer is used to return the number of bytes read from
// the given buffer. (It's only filled on a successful read)
// NOTE: the cinfo's 'offset' field is NOT set in this function.
comux_parse_result_t comux_cinfo_read_buffer(comux_cinfo_t* cinfo,
                                             char* buff, size_t buff_len,
                                             size_t* read_len);

// Takes in an open file descriptor and writes the cinfo struct's data buffer
// out to the file. Returns the number of bytes written. If the buffer was
// empty upon calling this function, 0 is returned.
size_t comux_cinfo_data_write(comux_cinfo_t* cinfo, int fd);

// Performs the same action as 'comux_cinfo_data_write', but instead writes to
// a buffer of maximum length 'max_len'.
// This function assumes 'buff' does not overlap with the cinfo's memory.
// Returns the number of bytes written, or a negative number indicating how
// much room was needed in total (when not enough room is available).
ssize_t comux_cinfo_data_write_buffer(comux_cinfo_t* cinfo, char* buff,
                                     size_t buff_len);

// A function to be called immediately after 'comux_cinfo_read' responsible for
// reading a chunk's data from the opened file descriptor and loading it into
// memory. Returns the number of bytes loaded into the given cinfo's 'data'
// buffer. If it returns less than cinfo->len, it's because the buffer enforced
// a maximum length.
// This function assumes cinfo->data, the buffer, has not yet been initialized
// and does not point to any heap-allocated memory.
size_t comux_cinfo_data_read(comux_cinfo_t* cinfo, int fd);

// Performs the same action as 'comux_cinfo_data_read', but instead reads from
// a buffer of max length 'buff_len'.
// If it returns less than cinfo->len, it's because the buffer enforced a
// maximum length OR 'buff_len' was smaller than 'cinfo->len'.
// This function assumes cinfo->data, the buffer, has not yet been initialized
// and does not point to any heap-allocated memory.
size_t comux_cinfo_data_read_buffer(comux_cinfo_t* cinfo, char* buff,
                                    size_t buff_len);

// Helper macro that calls buffer_append and increments the cinfo's length.
#define comux_cinfo_data_append(cinfo, string) do                           \
    {                                                                       \
        size_t bytes = buffer_append(&((comux_cinfo_t*) cinfo)->data, (char*) string); \
        ((comux_cinfo_t*) cinfo)->len += bytes;                             \
    } while (0)

// Helper macro that calls buffer_appendn and increments the cinfo's length.
#define comux_cinfo_data_appendn(cinfo, string, n) do                       \
    {                                                                       \
        size_t bytes = buffer_appendn(&((comux_cinfo_t*) cinfo)->data,      \
                                      (char*) string,                       \
                                      (size_t) n);                          \
        ((comux_cinfo_t*) cinfo)->len += bytes;                             \
    } while (0)

// Helper macro that calls buffer_appendf and increments the cinfo's length.
#define comux_cinfo_data_appendf(cinfo, format, ...) do                     \
    {                                                                       \
        size_t bytes = buffer_appendf(&((comux_cinfo_t*) cinfo)->data,      \
                                      (char*) format __VA_OPT__(,)          \
                                      __VA_ARGS__);                         \
        ((comux_cinfo_t*) cinfo)->len += bytes;                             \
    } while (0)

// Simple macro to take the cinfo's offset and add the correct number to point
// to the offset of the data segment.
#define comux_cinfo_data_offset(cinfo) (((comux_cinfo_t*) cinfo)->offset + 20)

// ========================= The Main Comux Struct ========================= //
// This struct, called the "manifest", represents the entire content of a comux
// file: the header information, information for each chunk, etc. This is
// used as a way to both create a new comux file and parse an existing one.
typedef struct comux_manifest
{
    comux_header_t header;  // the file's header information
    dllist_t cinfo_list;      // a list of chunk information structs
} comux_manifest_t;

// Initializes the given manifest to hold default values.
void comux_manifest_init(comux_manifest_t* manifest);

// Frees any internal pointers in the given manifest pointer. If 'free_ptrs'
// is non-zero, this function will also call free() on each of the cinfo
// struct pointers as it iterates across them.
void comux_manifest_free(comux_manifest_t* manifest, uint8_t free_ptrs);

// Takes a given cinfo and adds it to the manifest's cinfo list. This function
// assumes the 'cinfo' struct is heap-allocated or in a spot where its address
// won't be cleaned off of a stack somewhere.
void comux_manifest_cinfo_add(comux_manifest_t* manifest, comux_cinfo_t* cinfo);

// Takes in a chunk index and removes the chunk at the corresponding index
// from the manifest's cinfo list. If the index was out of bounds or the list
// was empty, NULL is returned. Otherwise, the cinfo that was removed is
// returned.
comux_cinfo_t* comux_manifest_cinfo_remove(comux_manifest_t* manifest, uint32_t idx);

// Takes in a comux struct and a file descriptor opened for writing and writes
// the header's contents out into the file. Returns the number of bytes
// written.
size_t comux_manifest_write(comux_manifest_t* manifest, int fd);

// Performs the same action as 'comux_manifest_write', but instead writes to a
// given buffer with a maximum length of 'max_len'.
// This function assumes 'buff' does not overlap with the manifest's memory.
// Returns the number of bytes written, or a negative number indicating not
// enough room was available.
ssize_t comux_manifest_write_buffer(comux_manifest_t* manifest, char* buff,
                                    size_t max_len);

// Takes in an open file descriptor for reading and attempts to read in an
// entire comux file. Returns COMUX_PARSE_OK on success, and another parse
// result code on error.
// If reading the entire file isn't an option, alternatively one could use:
//  1. comux_header_read()
//  2. comux_cinfo_read()
//  3. comux_cinfo_read()
//  4. ...
//  N. comux_cinfo_read()
comux_parse_result_t comux_manifest_read(comux_manifest_t* manifest, int fd);

// Performs the same action as 'comux_manifest_read', but instead reads from
// a buffer of max length 'buff_len'.
// The 'read_len' pointer is used to return the number of bytes read from
// the given buffer. (It's only filled on a successful read)
comux_parse_result_t comux_manifest_read_buffer(comux_manifest_t* manifest,
                                                char* buff, size_t buff_len,
                                                size_t* read_len);

#endif

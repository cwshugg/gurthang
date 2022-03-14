// Implements the function prototypes from comux.h.
//
//      Connor Shugg

// Module inclusions
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "comux.h"
#include "../utils/utils.h"


// ============================= Comux Parsing ============================= //
char* comux_parse_result_string(comux_parse_result_t result)
{
    // we'll use a big switch statement to find the right error code
    switch (result)
    {
        case COMUX_PARSE_OK:
            return "parsing successful";
        case COMUX_PARSE_EOF:
            return "reached end-of-file";
        case COMUX_PARSE_BAD_MAGIC:
            return "the comux header had an invalid magic field";
        case COMUX_PARSE_BAD_VERSION:
            return "the comux header had an invalid version field";
        case COMUX_PARSE_BAD_NUM_CONNS:
            return "the comux header had an invalid number-of-connections field";
        case COMUX_PARSE_BAD_NUM_CHUNKS:
            return "the comux header had an invalid number-of-chunks field";
        case COMUX_PARSE_BAD_CONN_ID:
            return "a comux chunk header had an invalid connection ID field";
        case COMUX_PARSE_BAD_CONN_LEN:
            return "a comux chunk header had an invalid data-length field";
        case COMUX_PARSE_BAD_CONN_SCHED:
            return "a comux chunk header had an invalid schedule field";
        case COMUX_PARSE_BAD_CONN_FLAGS:
            return "a comux chunk header had invalid flags";
        case COMUX_PARSE_CONN_LEN_MISMATCH:
            return "a comux chunk header's data length didn't match the number of bytes read";
        default:
            return "unknown parsing error";
    }
}


// =========================== The Comux Header ============================ //
void comux_header_init(comux_header_t* header)
{
    memcpy(header->magic, COMUX_MAGIC, COMUX_MAGIC_LEN); // copy magic value
    header->version = 0;
    header->num_conns = 0;
    header->num_chunks = 0;
}

void comux_header_free(comux_header_t* header)
{ /* presently unused, but here for the future */ }

size_t comux_header_write(comux_header_t* header, int fd)
{
    size_t total_bytes_written = 0;
    // first, write the magic bytes into the file
    ssize_t amount_written = write_check(fd, header->magic, COMUX_MAGIC_LEN);
    total_bytes_written += amount_written;

    // next, convert the version number to 4 bytes, then write them to the file
    uint8_t bytes[sizeof(uint32_t)];
    u32_to_bytes(header->version, bytes);
    amount_written = write_check(fd, bytes, sizeof(uint32_t));
    total_bytes_written += amount_written;

    // do the same for the field indicating the number of connections
    u32_to_bytes(header->num_conns, bytes);
    amount_written = write_check(fd, bytes, sizeof(uint32_t));
    total_bytes_written += amount_written;

    // do the same for the field indicating the number of chunks
    u32_to_bytes(header->num_chunks, bytes);
    amount_written = write_check(fd, bytes, sizeof(uint32_t));
    total_bytes_written += amount_written;

    return total_bytes_written;
}

ssize_t comux_header_write_buffer(comux_header_t* header, char* buff,
                                 size_t max_len)
{
    // compute the needed space in the buffer, and return if the buffer
    // doesn't have enough room
    ssize_t space_needed = COMUX_MAGIC_LEN +
                           sizeof(header->version) +
                           sizeof(header->num_conns) +
                           sizeof(header->num_chunks);
    if (max_len < space_needed)
    { return -space_needed; }

    // otherwise, we'll write each field out to the buffer
    ssize_t total_wcount = 0;
    uint8_t bytes[sizeof(uint32_t)];

    // write the magic
    memcpy(buff, header->magic, COMUX_MAGIC_LEN);
    total_wcount += COMUX_MAGIC_LEN;

    // write the version
    u32_to_bytes(header->version, bytes);
    memcpy(buff + total_wcount, bytes, sizeof(uint32_t));
    total_wcount += sizeof(uint32_t);

    // write num_conns
    u32_to_bytes(header->num_conns, bytes);
    memcpy(buff + total_wcount, bytes, sizeof(uint32_t));
    total_wcount += sizeof(uint32_t);

    // write num_chunks
    u32_to_bytes(header->num_chunks, bytes);
    memcpy(buff + total_wcount, bytes, sizeof(uint32_t));
    total_wcount += sizeof(uint32_t);

    return total_wcount;
}

comux_parse_result_t comux_header_read(comux_header_t* header, int fd)
{
    // first, attempt to read the magic bytes
    ssize_t amount_read = read_check(fd, header->magic, COMUX_MAGIC_LEN);
    
    // check invalid bytes. If we read less than what we expected, OR the bytes
    // we read don't match the expected magic value, return an error code
    if (amount_read == 0)
    { return COMUX_PARSE_EOF; }
    else  if (amount_read < COMUX_MAGIC_LEN)
    { return COMUX_PARSE_BAD_MAGIC; }
    else if (memcmp(header->magic, COMUX_MAGIC, COMUX_MAGIC_LEN))
    { return COMUX_PARSE_BAD_MAGIC; }

    // next, attempt to read four bytes (the 32-bit version number)
    uint8_t bytes[sizeof(uint32_t)];
    amount_read = read_check(fd, bytes, sizeof(uint32_t));

    // if we didn't read enough bytes, return an error code. Otherwise, parse
    // it as the comux version number
    if (amount_read == 0)
    { return COMUX_PARSE_EOF; }
    else if (amount_read < sizeof(uint32_t))
    { return COMUX_PARSE_BAD_VERSION; }
    header->version = bytes_to_u32(bytes);

    // read four more bytes (the 32-bit num_conns number)
    amount_read = read_check(fd, bytes, sizeof(uint32_t));

    // if we couldn't read enough bytes, return an error code. Otherwise, parse
    if (amount_read == 0)
    { return COMUX_PARSE_EOF; }
    else  if (amount_read < sizeof(uint32_t))
    { return COMUX_PARSE_BAD_NUM_CONNS; }
    header->num_conns = bytes_to_u32(bytes);

    // finally, read four more bytes (the 32-bit num_chunks number)
    amount_read = read_check(fd, bytes, sizeof(uint32_t));

    // if we couldn't read enough bytes, return an error code. Otherwise, parse
    if (amount_read == 0)
    { return COMUX_PARSE_EOF; }
    else  if (amount_read < sizeof(uint32_t))
    { return COMUX_PARSE_BAD_NUM_CHUNKS; }
    header->num_chunks = bytes_to_u32(bytes);

    return COMUX_PARSE_OK;
}

comux_parse_result_t comux_header_read_buffer(comux_header_t* header,
                                              char* buff, size_t buff_len,
                                              size_t* read_len)
{
    size_t total_rcount = 0;
    ssize_t space_remaining = buff_len;

    // first, try to read the magic bytes
    if ((space_remaining -= COMUX_MAGIC_LEN) < 0)
    { return COMUX_PARSE_BAD_MAGIC; }
    memcpy(header->magic, buff, COMUX_MAGIC_LEN);
    total_rcount += COMUX_MAGIC_LEN;
    // make sure we have the correct magic bytes
    if (memcmp(header->magic, COMUX_MAGIC, COMUX_MAGIC_LEN))
    { return COMUX_PARSE_BAD_MAGIC; }

    // next, try to read the version number
    uint8_t bytes[sizeof(uint32_t)];
    if ((space_remaining -= sizeof(uint32_t)) < 0)
    { return COMUX_PARSE_BAD_VERSION; }
    memcpy(bytes, buff + total_rcount, sizeof(uint32_t));
    header->version = bytes_to_u32(bytes);
    total_rcount += sizeof(uint32_t);

    // next, try to read the num_conns number
    if ((space_remaining -= sizeof(uint32_t)) < 0)
    { return COMUX_PARSE_BAD_NUM_CONNS; }
    memcpy(bytes, buff + total_rcount, sizeof(uint32_t));
    header->num_conns = bytes_to_u32(bytes);
    total_rcount += sizeof(uint32_t);

    // next, try to read the num_chunks number
    if ((space_remaining -= sizeof(uint32_t)) < 0)
    { return COMUX_PARSE_BAD_NUM_CHUNKS; }
    memcpy(bytes, buff + total_rcount, sizeof(uint32_t));
    header->num_chunks = bytes_to_u32(bytes);
    total_rcount += sizeof(uint32_t);

    // parsing went ok!
    *read_len = total_rcount;
    return COMUX_PARSE_OK;
}


// ====================== The Comux Connection Header ====================== //
void comux_cinfo_init(comux_cinfo_t* cinfo)
{
    cinfo->id = 0;
    cinfo->len = 0;
    cinfo->sched = 0;
    cinfo->flags = COMUX_CHUNK_FLAGS_NONE;
    buffer_init(&cinfo->data, 0); // start with an empty buffer
    cinfo->offset = 0; // filled in later when writing or reading
}

void comux_cinfo_free(comux_cinfo_t* cinfo)
{
    buffer_free(&cinfo->data);
}

size_t comux_cinfo_write(comux_cinfo_t* cinfo, int fd)
{
    size_t total_bytes_written = 0;
    uint8_t bytes[sizeof(uint64_t)];

    // set the cinfo's offset field based on the file descriptor
    cinfo->offset = lseek(fd, 0, SEEK_CUR);
    if (cinfo->offset == -1)
    { /* fatality_errno(errno, "failed to get current cinfo offset"); */ }

    // attempt to write the connection ID
    u32_to_bytes(cinfo->id, bytes);
    ssize_t amount_written = write_check(fd, bytes, sizeof(uint32_t));
    total_bytes_written += amount_written;

    // attempt to write the chunk data length
    u64_to_bytes(cinfo->len, bytes);
    amount_written = write_check(fd, bytes, sizeof(uint64_t));
    total_bytes_written += amount_written;

    // attempt to write the chunk schedule field
    u32_to_bytes(cinfo->sched, bytes);
    amount_written = write_check(fd, bytes, sizeof(uint32_t));
    total_bytes_written += amount_written;

    // attempt to write the chunk flags field
    u32_to_bytes(cinfo->flags, bytes);
    amount_written = write_check(fd, bytes, sizeof(uint32_t));
    total_bytes_written += amount_written;

    return total_bytes_written;
}

ssize_t comux_cinfo_write_buffer(comux_cinfo_t* cinfo, char* buff,
                                 size_t max_len)
{
    // compute the needed space in the buffer. Return if there's not enough
    ssize_t space_needed = sizeof(cinfo->id) +
                           sizeof(cinfo->len) +
                           sizeof(cinfo->sched) +
                           sizeof(cinfo->flags);
    if (max_len < space_needed)
    { return -space_needed; }

    // otherwise, we'll write each field out to the buffer
    ssize_t total_wcount = 0;
    uint8_t bytes[sizeof(uint64_t)];

    // write the connection ID
    u32_to_bytes(cinfo->id, bytes);
    memcpy(buff + total_wcount, bytes, sizeof(uint32_t));
    total_wcount += sizeof(uint32_t);

    // write the chunk data length
    u64_to_bytes(cinfo->len, bytes);
    memcpy(buff + total_wcount, bytes, sizeof(uint64_t));
    total_wcount += sizeof(uint64_t);

    // write the schedule field
    u32_to_bytes(cinfo->sched, bytes);
    memcpy(buff + total_wcount, bytes, sizeof(uint32_t));
    total_wcount += sizeof(uint32_t);

    // write the flags field
    u32_to_bytes(cinfo->flags, bytes);
    memcpy(buff + total_wcount, bytes, sizeof(uint32_t));
    total_wcount += sizeof(uint32_t);

    return total_wcount;
}

comux_parse_result_t comux_cinfo_read(comux_cinfo_t* cinfo, int fd)
{
    uint8_t bytes[sizeof(uint64_t)];

    // set the cinfo's offset field
    cinfo->offset = lseek(fd, 0, SEEK_CUR);
    if (cinfo->offset == -1)
    { /* fatality_errno(errno, "failed to get current cinfo offset"); */ }

    // first, try to read four bytes as the chunk ID
    ssize_t amount_read = read_check(fd, bytes, sizeof(uint32_t));
    if (amount_read == 0)
    { return COMUX_PARSE_EOF; }
    else if (amount_read < sizeof(uint32_t))
    { return COMUX_PARSE_BAD_CONN_ID; }
    cinfo->id = bytes_to_u32(bytes);

    // next, try to read eight bytes as the data length
    amount_read = read_check(fd, bytes, sizeof(uint64_t));
    if (amount_read == 0)
    { return COMUX_PARSE_EOF; }
    else if (amount_read < sizeof(uint64_t))
    { return COMUX_PARSE_BAD_CONN_LEN; }
    cinfo->len = bytes_to_u64(bytes);

    // next, try to read four bytes as the chunk schedule number
    amount_read = read_check(fd, bytes, sizeof(uint32_t));
    if (amount_read == 0)
    { return COMUX_PARSE_EOF; }
    else  if (amount_read < sizeof(uint32_t))
    { return COMUX_PARSE_BAD_CONN_SCHED; }
    cinfo->sched = bytes_to_u32(bytes);

    // finally, try to read four bytes as the chunk flag bitfield
    amount_read = read_check(fd, bytes, sizeof(uint32_t));
    if (amount_read == 0)
    { return COMUX_PARSE_EOF; }
    else  if (amount_read < sizeof(uint32_t))
    { return COMUX_PARSE_BAD_CONN_FLAGS; }
    cinfo->flags = bytes_to_u32(bytes);

    return COMUX_PARSE_OK;
}

comux_parse_result_t comux_cinfo_read_buffer(comux_cinfo_t* cinfo,
                                             char* buff, size_t buff_len,
                                             size_t* read_len)
{
    size_t total_rcount = 0;
    ssize_t space_remaining = buff_len;
    uint8_t bytes[sizeof(uint64_t)];

    // first, try to read the connection ID
    if ((space_remaining -= sizeof(uint32_t)) < 0)
    { return COMUX_PARSE_BAD_CONN_ID; }
    memcpy(bytes, buff + total_rcount, sizeof(uint32_t));
    cinfo->id = bytes_to_u32(bytes);
    total_rcount += sizeof(uint32_t);

    // next, try to read the data length
    if ((space_remaining -= sizeof(uint64_t)) < 0)
    { return COMUX_PARSE_BAD_CONN_LEN; }
    memcpy(bytes, buff + total_rcount, sizeof(uint64_t));
    cinfo->len = bytes_to_u64(bytes);
    total_rcount += sizeof(uint64_t);

    // next, try to read the chunk scheduling number
    if ((space_remaining -= sizeof(uint32_t)) < 0)
    { return COMUX_PARSE_BAD_CONN_SCHED; }
    memcpy(bytes, buff + total_rcount, sizeof(uint32_t));
    cinfo->sched = bytes_to_u32(bytes);
    total_rcount += sizeof(uint32_t);

    // finally, try to read the flags
    if ((space_remaining -= sizeof(uint32_t)) < 0)
    { return COMUX_PARSE_BAD_CONN_FLAGS; }
    memcpy(bytes, buff + total_rcount, sizeof(uint32_t));
    cinfo->flags = bytes_to_u32(bytes);
    total_rcount += sizeof(uint32_t);

    *read_len = total_rcount;
    return COMUX_PARSE_OK;
}

size_t comux_cinfo_data_write(comux_cinfo_t* cinfo, int fd)
{
    // get a pointer to the buffer's data, then choose a chunk size to write
    // bytes out to the file
    char* data = buffer_dptr(&cinfo->data);
    size_t chunk_size = cinfo->len > 2048 ? 2048 : cinfo->len;

    // write until all bytes have been written
    ssize_t wcount = 0;
    size_t total_wcount = 0;
    while ((wcount = write(fd, data + total_wcount,
            MIN(cinfo->len - total_wcount, chunk_size))) > 0)
    { total_wcount += wcount; }
    // check for write error
    if (wcount == -1)
    { fatality_errno(errno, "failed to write bytes to fd %d", fd); }

    return total_wcount;
}

ssize_t comux_cinfo_data_write_buffer(comux_cinfo_t* cinfo, char* buff,
                                     size_t buff_len)
{
    // determine how much space we need, and return if we don't have enough
    ssize_t space_needed = cinfo->len;
    if (space_needed > buff_len)
    { return -space_needed; }

    // otherwise, we'll copy the cinfo's entire data buffer into the buffer
    char* data = buffer_dptr(&cinfo->data);
    memcpy(buff, data, space_needed);
    return space_needed;
}

size_t comux_cinfo_data_read(comux_cinfo_t* cinfo, int fd)
{
    // first, check the length of the 'cinfo' object. This should have just
    // been parsed in 'comux_cinfo_read' and should explain the number of bytes
    // in this chunk's data segment. If it's way too big, we're going to have
    // to cap it off
    uint64_t cap = cinfo->len > COMUX_CHUNK_DATA_MAXLEN ?
                   COMUX_CHUNK_DATA_MAXLEN : cinfo->len;

    // we'll assume the buffer hasn't had any memory allocated yet. Initialize
    // to hold the correct amount of memory
    buffer_init(&cinfo->data, cap);

    // set up a buffer for reading data from the file in chunks
    size_t buff_size = cap > 2048 ? 2048 : cap;
    char buff[buff_size];
    ssize_t rcount = 0;
    size_t total_rcount = 0;
    // enter a reading loop until we hit an error or we've read in 'cap' bytes
    while ((rcount = read(fd, buff, MIN(cap - total_rcount, buff_size))) > 0 &&
           total_rcount < cap)
    {
        // append the buffer's contents to the cinfo's data buffer
        comux_cinfo_data_appendn(cinfo, buff, rcount);
        total_rcount += rcount;
    }
    // check for read-error
    if (rcount == -1)
    { fatality_errno(errno, "failed to read bytes from fd %d", fd); }

    cinfo->len = total_rcount;
    return total_rcount;
}

size_t comux_cinfo_data_read_buffer(comux_cinfo_t* cinfo, char* buff,
                                    size_t buff_len)
{
    // check the length of the cinfo object and use it to determine a maximum
    // number of bytes to read
    uint64_t cap = cinfo->len > COMUX_CHUNK_DATA_MAXLEN ?
                   COMUX_CHUNK_DATA_MAXLEN : cinfo->len;
    cap = MIN(cap, (uint64_t) buff_len);
    
    // initialize the buffer (assume it hasn't been initialized yet), then copy
    // the given buffer's contents into it
    buffer_init(&cinfo->data, cap);
    comux_cinfo_data_appendn(cinfo, buff, cap);
    cinfo->len = cap;
    return cap;
}


// ========================= The Main Comux Struct ========================= //
void comux_manifest_init(comux_manifest_t* manifest)
{
    comux_header_init(&manifest->header);
    dllist_init(&manifest->cinfo_list);
}

void comux_manifest_free(comux_manifest_t* manifest, uint8_t free_ptrs)
{
    // free the header
    comux_header_free(&manifest->header);

    // iterate until the cinfo list is empty
    while (manifest->cinfo_list.size > 0)
    {
        // pop the entry, get a reference to the cinfo struct, then free
        dllist_elem_t* e = dllist_pop_head(&manifest->cinfo_list);
        comux_cinfo_t* cinfo = e->container;
        comux_cinfo_free(cinfo);
        manifest->header.num_chunks--;

        // if 'free_ptrs' is set, we'll free 'cinfo' as well
        if (free_ptrs) { free(cinfo); }
    }
}

void comux_manifest_cinfo_add(comux_manifest_t* manifest, comux_cinfo_t* cinfo)
{
    // simply add the struct to the list's rear
    dllist_push_tail(&manifest->cinfo_list, &cinfo->elem, cinfo);
    manifest->header.num_chunks++;
}

comux_cinfo_t* comux_manifest_cinfo_remove(comux_manifest_t* manifest, uint32_t idx)
{
    // first, check the index - if it's out of bounds, return NULL
    if (idx < 0 || idx >= manifest->cinfo_list.size)
    { return NULL; }

    // otherwise, iterate through the list until we land on the right index
    uint32_t i = 0;
    dllist_elem_t* e;
    dllist_iterate_manual(&manifest->cinfo_list, e)
    {
        // grab the cinfo struct pointer and move 'e' to the next entry
        comux_cinfo_t* cinfo = e->container;
        e = e->next;

        // if we're at the right index, remove it and return
        if (i++ == idx)
        {
            dllist_remove(&manifest->cinfo_list, &cinfo->elem);
            manifest->header.num_chunks--;
            return cinfo;
        }
    }

    // if we reached here without removing and returning, we can't find it
    return NULL;
}

size_t comux_manifest_write(comux_manifest_t* manifest, int fd)
{
    // first, write the header out to the file
    size_t total_bytes_written = comux_header_write(&manifest->header, fd);

    // next, iterate acorss the list of chunks
    dllist_elem_t* e;
    dllist_iterate(&manifest->cinfo_list, e)
    {
        // write out the cinfo's header, then write its data
        comux_cinfo_t* cinfo = e->container;
        total_bytes_written += comux_cinfo_write(cinfo, fd);
        total_bytes_written += comux_cinfo_data_write(cinfo, fd);
    }

    return total_bytes_written;
}

ssize_t comux_manifest_write_buffer(comux_manifest_t* manifest, char* buff,
                                    size_t max_len)
{
    ssize_t space_remaining = max_len;
    ssize_t total_wcount = 0;

    // first, attempt to write the header
    ssize_t wcount = comux_header_write_buffer(&manifest->header,
                                               buff, space_remaining);
    if (wcount < 0) { return wcount; }
    total_wcount += wcount;
    space_remaining = MAX(0, space_remaining - wcount);

    // next, iterate across all chunks and write them out
    dllist_elem_t* e;
    dllist_iterate(&manifest->cinfo_list, e)
    {
        comux_cinfo_t* cinfo = e->container;
        // first, try to write out the cinfo's header
        wcount = comux_cinfo_write_buffer(cinfo, buff + total_wcount,
                                          space_remaining);
        if (wcount < 0) { return wcount; }
        total_wcount += wcount;
        space_remaining = MAX(0, space_remaining - wcount);

        // next, try to write out the data
        wcount = comux_cinfo_data_write_buffer(cinfo, buff + total_wcount,
                                               space_remaining);
        if (wcount < 0) { return wcount; }
        total_wcount += wcount;
        space_remaining = MAX(0, space_remaining - wcount);
    }

    return total_wcount;
}

comux_parse_result_t comux_manifest_read(comux_manifest_t* manifest, int fd)
{
    // first, try to read the header (return any bad parse codes)
    comux_parse_result_t result = comux_header_read(&manifest->header, fd);
    if (result)
    { return result; }

    // next, iterate and try to read each chunk info section
    while (!result)
    {
        // allocate a new struct and try to read the chunk header.
        // If parsing fails, free and continue
        comux_cinfo_t* cinfo = alloc_check(sizeof(comux_cinfo_t));
        comux_cinfo_init(cinfo);
        result = comux_cinfo_read(cinfo, fd);
        if (result)
        {
            comux_cinfo_free(cinfo);
            free(cinfo);
            continue;
        }

        // otherwise, try to read the chunk's data. If we couldn't read enough,
        // set the error code, free and continue
        uint64_t expected_len = cinfo->len;
        size_t rcount = comux_cinfo_data_read(cinfo, fd);
        if (rcount < expected_len)
        {
            comux_cinfo_free(cinfo);
            free(cinfo);
            result = COMUX_PARSE_CONN_LEN_MISMATCH;
            continue;
        }

        // if it all succeeded, push this cinfo onto the manifest's list
        dllist_push_tail(&manifest->cinfo_list, &cinfo->elem, cinfo);
    }

    // if we reached end-of-file and no other error codes were returned, we'll
    // return PARSE_OK
    return result == COMUX_PARSE_EOF ? COMUX_PARSE_OK : result;
}

comux_parse_result_t comux_manifest_read_buffer(comux_manifest_t* manifest,
                                                char* buff, size_t buff_len,
                                                size_t* read_len)
{
    size_t total_rcount = 0;

    // first, try to read the header
    size_t rcount = 0;
    comux_parse_result_t result = comux_header_read_buffer(&manifest->header,
                                                           buff, buff_len,
                                                           &rcount);
    if (result)
    { return result; }
    total_rcount += rcount;

    // next, iterate and try to read each chunk info section
    for (uint32_t i = 0; i < manifest->header.num_chunks && !result; i++)
    {
        // allocate a new cinfo struct and read its information. On failure,
        // free and continue
        comux_cinfo_t* cinfo = alloc_check(sizeof(comux_cinfo_t));
        comux_cinfo_init(cinfo);
        result = comux_cinfo_read_buffer(cinfo, buff + total_rcount,
                                         buff_len - total_rcount, &rcount);
        if (result)
        {
            comux_cinfo_free(cinfo);
            free(cinfo);
            continue;
        }
        total_rcount += rcount;

        // attempt to read the cinfo's data from the buffer. If we couldn't
        // read enough, free and continue
        uint64_t expected_len = cinfo->len;
        rcount = comux_cinfo_data_read_buffer(cinfo, buff + total_rcount,
                                              buff_len - total_rcount);
        if (rcount < expected_len)
        {
            comux_cinfo_free(cinfo);
            free(cinfo);
            result = COMUX_PARSE_CONN_LEN_MISMATCH;
            continue;
        }
        total_rcount += rcount;

        // if it was all parsed successfully, push onto the list
        dllist_push_tail(&manifest->cinfo_list, &cinfo->elem, cinfo);
    }
    
    *read_len = total_rcount;
    return COMUX_PARSE_OK;
}

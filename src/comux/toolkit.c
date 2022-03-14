// This comux "toolkit" source file implements a simple command-line tool to
// create and modify comux files. This is useful for generating input sets
// for AFL++ before launch. It's also useful to me when I test code.
//
//      Connor Shugg

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "comux.h"
#include "../utils/utils.h"

// Defines/macros
#define FILE_PATH_MAXLEN 512
#define DETAILS_MAXLEN 512
#define C_NONE "\033[0m"
#define C_GRAY "\033[90m"

// Globals
static char outfile[FILE_PATH_MAXLEN] = {'\0'}; // contains output file path
static char infile[FILE_PATH_MAXLEN] = {'\0'};  // contains input file path

// Connection-specific settings
static uint32_t scheduling = 0; // used as the scheduling value for new conns
static uint8_t scheduling_touched = 0; // indicator if 'scheduling' was set
static uint32_t flags = COMUX_CHUNK_FLAGS_NONE; // used as flags for new conns
static uint8_t flags_touched = 0; // indicator if 'flags' was set
static uint32_t cid = 0;
static uint8_t cid_touched = 0; // indicator if 'cid' was set

// ========================= Command-Line Options ========================== //
static uint8_t verbose = 0; // enabled if -v / --verbose is given

// Set up an array of command-line options
static struct option clopts[] =
{
    {"show", no_argument, 0, 's'},              // read and parse existing file
    {"convert", no_argument, 0, 'c'},           // convert plain file to comux
    {"add-chunk", required_argument, 0, 'a'},   // add chunk to comux file
    {"rm-chunk", required_argument, 0, 'r'},    // remove chunk from file
    {"extract-chunk", required_argument, 0, 'x'}, // extract data from conn
    {"edit-chunk", required_argument, 0, 'e'},  // edit an existing chunk
    {"infile", required_argument, 0, 'i'},      // specify the input file
    {"outfile", required_argument, 0, 'o'},     // specify the output file
    {"set-conn", required_argument, 0, 'C'},    // specify the connection ID
    {"set-sched", required_argument, 0, 'S'},   // set a chunk's sched field
    {"set-flags", required_argument, 0, 'F'},   // set a chunk's flags
    {"set-num-conns", required_argument, 0, 'N'}, // set the number of conns
    {"verbose", no_argument, 0, 'v'}            // verbose printing
};

// Array of descriptions for the command-line arguments
static char* clopts_descriptions[] =
{
    "Reads a comux file and prints out a summary.",
    "Takes a plain file and converts it to a comux file with a single connection and single chunk.",
    "(ARG=file_path) Adds a new chunk to an existing comux file.",
    "(ARG=chunk_index) Removes a chunk from an existing comux file.",
    "(ARG=chunk_index) Extracts the data from a specific chunk in an existing comux file.",
    "(ARG=chunk_index) Edits the data or header fields of a chunk in an existing comux file.",
    "(ARG=file_path) Specifies the input file to read from. (Input will come from stdin if not specified.)",
    "(ARG=file_path) Specifies the output file to write to. (Output will go to stdout if not specified.)",
    "(ARG=conn_ID) Specifies the connection ID to set for a chunk (used with -c, -a, -e)",
    "(ARG=sched_value) Specifies the scheduling value to set for a chunk (used with -c, -a, -e)",
    "(ARG=flags_value) Specifies the flags to set for a chunk (used with -c, -a, -e)",
    "(ARG=num_conns) Sets a comux file's 'num_conns' header value.",
    "Enables verbose output. (Chunk data segments will be printed.)"
};

// Number of command-line options
static int clopts_len = 13;

static void usage(char* execname)
{
    printf("Usage: %s [-i infile] [-o outfile] [OPTIONS]\n"
           "If INFILE is not specified, input will be read from stdin.\n"
           "Command-Line Options:\n",
           execname);
    
    for (uint8_t i = 0; i < clopts_len; i++)
    {
        if (clopts[i].val == 'a' || clopts[i].val == 'i' ||
            clopts[i].val == 'C' || clopts[i].val == 'N' ||
            clopts[i].val == 'v')
        { printf(STAB_TREE3 "\n"); }

        // print the command-line option
        printf("%s-%c%s / --%-16s %s\n",
               i < clopts_len - 1 ? STAB_TREE2 : STAB_TREE1,
               clopts[i].val, 
               clopts[i].has_arg ? " ARG" : "    ",
               clopts[i].name,
               clopts_descriptions[i]);
    }

    printf("If you're having trouble, try running with -v to see extra information.\n");
}


// ============================= Other Helpers ============================= //
// Helper verbose-printing macro, given a file stream (such as stdout/stderr).
#define vprintf(stream, format, ...) \
        if (verbose) \
        { fprintf(stream, (char*) format __VA_OPT__(,) __VA_ARGS__); }

static void parse_conn_id(char* val)
{
    long conn;
    if (str_to_int(val, &conn))
    { fatality("failed to parse an connection ID from \"%s\".", val); }

    // if the value is less than zero, complain
    if (conn < 0)
    { fatality("the connection ID must be zero or greater."); }

    // set the global variable and increment the helper counter
    cid = (uint32_t) conn;
    cid_touched++;
}

// Attempts to parse a string into an integer, storing it in the 'scheduling'
// global variable.
static void parse_scheduling(char* val)
{
    long sched;
    if (str_to_int(val, &sched))
    { fatality("failed to parse an scheduling value from \"%s\".", val); }

    // if the value is less than zero, complain
    if (sched < 0)
    { fatality("the scheduling value must be zero or greater."); }

    // set the global variable and increment the helper counter
    scheduling = (uint32_t) sched;
    scheduling_touched++;
}

// Parses the given string into flags and updates the 'flags' global variable.
static void parse_flags(char* val)
{
    uint32_t flag_count = 0;

    // the user should have supplied flags as a comma-separated string, with
    // the name of each flag specified. We'll first try to split this up by
    // commas
    char* flag_name = NULL;
    char* str = val;
    while ((flag_name = strtok(str, ",")) != NULL)
    {
        // check the special 'NONE' case (to clear all flags)
        if (!strcmp(flag_name, "NONE"))
        {
            vprintf(stderr, C_GRAY "Special 'NONE' flag found. "
                    "All flags will be cleared for the specified chunk.\n"
                    C_NONE);
            flags = COMUX_CHUNK_FLAGS_NONE;
            break;
        }
        uint32_t flag = 0;

        // otherwise, attempt to match the flag name up with a specific flag
        if (!strcmp(flag_name, "AWAIT_RESPONSE"))
        { flag = COMUX_CHUNK_FLAGS_AWAIT_RESPONSE; }
        else if (!strcmp(flag_name, "NO_SHUTDOWN"))
        { flag = COMUX_CHUNK_FLAGS_NO_SHUTDOWN; }

        // OR the flag with the global flag field. If a flag wasn't actually
        // found, print a warning
        flags |= flag;
        if (!flag)
        { vprintf(stderr, C_GRAY "Warning: unknown flag: '%s'\n" C_NONE, flag_name); }

        str = NULL;
        flag_count++;
    }


    // set the global and increment the helper counter
    flags_touched = flag_count > 0;
}


// =========================== File I/O Helpers ============================ //
// Uses the 'outfile' global to determine which file descriptor output should
// be written to (if not 'outfile', then stdout)
static int io_out_get_fd()
{
    // if the outfile is set, try to open a descriptor to it for writing
    if (*outfile != '\0')
    {
        int fd = open(outfile, O_CREAT | O_WRONLY, 0644);
        if (fd == -1)
        { fatality_errno(errno, "failed to open file for writing: %s.", outfile); }
        return fd;
    }

    // if not, return stdout's fd
    return STDOUT_FILENO;
}

// Passes in the file descriptor from io_out_get_fd() and closes it, if necessary.
static void io_out_close_fd(int fd)
{
    if (fd == STDOUT_FILENO)
    { return; }
    
    // close the non-stdout file descriptor
    if (close(fd) == -1)
    { fatality_errno(errno, "failed to close file descriptor: %d", fd); }
}

// Uses the 'infile' global to determine which file descriptor input should be
// read from (if not 'infile', then stdin)
static int io_in_get_fd()
{
    // if the infile is set, try to open a descriptor to it for reading
    if (*infile != '\0')
    {
        int fd = open(infile, O_RDONLY);
        if (fd == -1)
        { fatality_errno(errno, "failed to open file for reading: %s.", infile); }
        return fd;
    }

    // if not, return stdin's fd
    return STDIN_FILENO;
}

// Passes in the fd returned from io_in_get_fd() and closes it, if necessary.
static void io_in_close_fd(int fd)
{
    if (fd == STDIN_FILENO)
    { return; }
    
    // close the non-stdout file descriptor
    if (close(fd) == -1)
    { fatality_errno(errno, "failed to close file descriptor: %d", fd); }
}


// ======================== Comux Parsing/Modifying ======================== //
// Helper function that takes a manifest and cinfo struct, reads bytes from
// stdin and adds it to the cinfo's buffer, then adds the cinfo to the
// manifest.
static void comux_cinfo_read_input(comux_cinfo_t* cinfo)
{
    // set up a buffer and some loop variables
    size_t buff_len = 1024;
    char buff[buff_len];
    ssize_t rcount = 0;
    size_t total_rcount = 0;
    uint8_t capped = 0;

    // get a file descriptor to read from
    int infd = io_in_get_fd();

    // read until the file is exhausted, or we've capped it off at a max size
    while ((rcount = read(infd, buff, buff_len)) > 0 && !capped)
    {
        // determine if we need to cap it off, based on the length of stdin
        ssize_t append_len = rcount;
        if (total_rcount + rcount > COMUX_CHUNK_DATA_MAXLEN)
        {
            fprintf(stderr, C_GRAY "Warning: capping off at %d bytes.\n" C_NONE,
                    COMUX_CHUNK_DATA_MAXLEN);
            append_len = COMUX_CHUNK_DATA_MAXLEN - (total_rcount + rcount);
            capped = 1;
        }

        // append the bytes to the cinfo's buffer
        if (append_len > 0)
        {
            comux_cinfo_data_appendn(cinfo, buff, append_len);
            total_rcount += rcount;
        }
    }
    // check for read error
    if (rcount == -1)
    { fatality_errno(errno, "failed to read bytes from stdin"); }

    // close the file descriptor
    io_in_close_fd(infd);
}


// ========================== Main Functionality =========================== //
// Implements the -s / --show functionality.
static void comux_show(char* arg)
{
    vprintf(stderr, C_GRAY "Reading input via %s...\n" C_NONE,
            *infile == '\0' ? "stdin" : infile);
    // initialize a manifest struct
    comux_manifest_t manifest;
    comux_manifest_init(&manifest);

    // get a file descriptor to read from
    int infd = io_in_get_fd();

    // ----------------- parse header ------------------ //
    // read the header from stdin (check for parse error)
    comux_parse_result_t res = comux_header_read(&manifest.header, infd);
    if (res)
    {
        fatality("failed to parse the header: %s.",
                 comux_parse_result_string(res));
    }

    // print out header information
    printf("* COMUX [version: %u] [num_connections: %u] [num_chunks: %u]\n",
           manifest.header.version, manifest.header.num_conns,
           manifest.header.num_chunks);
    
    // ----------------- parse chunks ------------------ //
    // iterate across the number of chunks specified by the header
    for (uint32_t i = 0; i < manifest.header.num_chunks; i++)
    {
        // initialize a cinfo object and attempt to parse from stdin
        comux_cinfo_t cinfo;
        comux_cinfo_init(&cinfo);
        res = comux_cinfo_read(&cinfo, infd);
        if (res)
        {
            fatality("failed to parse chunk %d: %s.", i,
                     comux_parse_result_string(res));
        }

        // print out the cinfo's information
        printf("* CHUNK %d: conn_id=%u, data_length=%lu, scheduling=%u, flags=0x%x\n",
               i, cinfo.id, cinfo.len, cinfo.sched, cinfo.flags);
        
        // if verbose is enabled, we'll actually read the data segment and
        // print it out. If not, we'll just seek past it
        if (verbose)
        {
            size_t rcount = comux_cinfo_data_read(&cinfo, infd);
            printf("%s\n", buffer_dptr(&cinfo.data));

            // if the amount of bytes read was truncated for some reason, print
            // a warning to the user
            if (rcount < cinfo.len)
            {
                fprintf(stderr, C_GRAY
                       "! Only %ld bytes were read (the chunk header specified %lu).\n"
                       "! Perhaps data was too long, or the file ended too early?\n" C_NONE,
                       rcount, cinfo.len);
            }
        }
        else
        {
            // seek past the current chunk's data segment to get to the next
            // chunk's header
            if (lseek(infd, cinfo.len, SEEK_CUR) == -1)
            {
                fatality_errno(errno, "failed to seek past chunk %d data segment",
                               i);
            }
        }

        // free memory
        comux_cinfo_free(&cinfo);
    }

    // close the file descriptor and free memory
    io_in_close_fd(infd);
    comux_manifest_free(&manifest, 0);
}

// Implements the -c / --convert functionality.
static void comux_convert(char* arg)
{
    // create a new manifest
    comux_manifest_t manifest;
    comux_manifest_init(&manifest);

    // create a new chunk info struct
    comux_cinfo_t cinfo;
    comux_cinfo_init(&cinfo);

    // set chunk fields
    cinfo.id = cid;
    cinfo.sched = scheduling;
    cinfo.flags = flags;
    
    vprintf(stderr, C_GRAY
            "This will format your input as a comux file with a single connection and single chunk.\n"
            "Awaiting comux chunk content via stdin...\n" C_NONE);
    
    // read from stdin and add a single chunk, then add it to the manifest
    comux_cinfo_read_input(&cinfo);
    comux_manifest_cinfo_add(&manifest, &cinfo);
    manifest.header.num_conns = 1;
    
    // write the resulting comux file to output
    int outfd = io_out_get_fd();    // open file descriptor
    comux_manifest_write(&manifest, outfd);
    io_out_close_fd(outfd);         // close the file descriptor
    
    vprintf(stderr, C_GRAY "Comux format written to %s.\n" C_NONE,
            outfd == STDOUT_FILENO ? "stdout" : outfile);
    
    // free memory
    comux_manifest_free(&manifest, 0);
}

// Implements the -a / --add-chunk functionality.
static void comux_add_chunk(char* in)
{
    // first, create a new cinfo struct
    comux_cinfo_t cnew;
    comux_cinfo_init(&cnew);
    
    vprintf(stderr, C_GRAY
            "This will read your input and add a new chunk to the comux data in %s.\n"
            "Reading new comux chunk content via %s...\n" C_NONE,
            in, *infile == '\0' ? "stdin" : infile);
    // read from stdin and store it in the new cinfo struct
    comux_cinfo_read_input(&cnew);

    // next, open a file descriptor to read the existing file
    int infd = open(in, O_RDONLY);
    if (infd == -1)
    { fatality_errno(errno, "failed to open file for reading: %s", in); }

    // open another file descriptor, used to write output
    int outfd = io_out_get_fd();

    // next, make a manifest and parse/write the header
    comux_manifest_t manifest;
    comux_manifest_init(&manifest);

    // parse the header, increase its 'num_chunks' field, then write it back out
    comux_parse_result_t res = comux_header_read(&manifest.header, infd);
    if (res)
    { fatality("failed to parse the header: %s.", comux_parse_result_string(res)); }
    uint32_t num_original_chunks = manifest.header.num_chunks++;
    comux_header_write(&manifest.header, outfd);

    // iterate through each existing comux chunk, read it, then write it out
    for (uint32_t i = 0; i < num_original_chunks; i++)
    {
        comux_cinfo_t cinfo;
        comux_cinfo_init(&cinfo);

        // read the cinfo header and write it back out
        res = comux_cinfo_read(&cinfo, infd);
        if (res)
        {
            fatality("failed to parse chunk %d: %s.", i,
                     comux_parse_result_string(res));
        }
        comux_cinfo_write(&cinfo, outfd);

        // read the cinfo data and write it back out
        comux_cinfo_data_read(&cinfo, infd);
        comux_cinfo_data_write(&cinfo, outfd);

        // finally, free the memory
        comux_cinfo_free(&cinfo);
    }

    // set other fields
    cnew.id = cid;
    cnew.sched = scheduling;
    cnew.flags = flags;

    // write the chunk header and the chunk data to output
    comux_cinfo_write(&cnew, outfd);
    comux_cinfo_data_write(&cnew, outfd);

    // close the file descriptors and free memory
    io_out_close_fd(outfd);
    io_in_close_fd(infd);
    comux_manifest_free(&manifest, 0);
    comux_cinfo_free(&cnew);
}

// Implements the -r / --rm-chunk functionality.
static void comux_rm_chunk(char* in)
{
    // attempt to parse the given argument as an index
    long idx_converted;
    if (str_to_int(in, &idx_converted))
    { fatality("failed to parse a positive chunk index from \"%s\".", in); }
    uint32_t idx = (uint32_t) idx_converted;

    // next, get a file descriptor to read input from and write output to
    int infd = io_in_get_fd();
    int outfd = io_out_get_fd();

    // read the comux header
    comux_manifest_t manifest;
    comux_manifest_init(&manifest);
    comux_parse_result_t res = comux_header_read(&manifest.header, infd);
    if (res)
    { fatality("failed to parse the header: %s.", comux_parse_result_string(res)); }

    // if the index is out of bounds, complain
    if (idx < 0 || idx >= manifest.header.num_chunks)
    {
        fatality("the chunk index must be between 0 and %d (inclusive)",
                 manifest.header.num_chunks - 1);
    }

    // adjust the header's 'num_chunks' field, then write it out
    uint32_t write_count = manifest.header.num_chunks;
    manifest.header.num_chunks--;
    comux_header_write(&manifest.header, outfd);

    // then, iterate through each chunk. We'll read each one, then write
    // it to output ONLY IF it's not the index we want to remove
    for (uint32_t i = 0; i < write_count; i++)
    {
        comux_cinfo_t cinfo;
        comux_cinfo_init(&cinfo);

        // parse the next chunk header
        res = comux_cinfo_read(&cinfo, infd);
        if (res)
        {
            fatality("failed to parse chunk %d: %s.", i,
                     comux_parse_result_string(res));
        }

        // determine if this is the chunk we want to remove, then read
        // and write (only write if we're NOT removing this one)
        uint8_t match = i == idx;
        if (!match)
        { comux_cinfo_write(&cinfo, outfd); }
        comux_cinfo_data_read(&cinfo, infd);
        if (!match)
        { comux_cinfo_data_write(&cinfo, outfd); }

        // free memory
        comux_cinfo_free(&cinfo);
    }

    // close the file descriptors and free memory
    io_out_close_fd(outfd);
    io_in_close_fd(infd);
    comux_manifest_free(&manifest, 0);
}

// Implements the -x / --extract-chunk functionality.
static void comux_extract_chunk(char* in)
{
    // first, try to parse the string as an id number
    long idx_converted;
    if (str_to_int(in, &idx_converted))
    { fatality("failed to parse a positive chunk index from \"%s\".", in); }
    uint32_t idx = (uint32_t) idx_converted;

    // next, open a file descriptor to read from
    int infd = io_in_get_fd();

    // read the manifest
    comux_manifest_t manifest;
    comux_manifest_init(&manifest);
    comux_parse_result_t res = comux_header_read(&manifest.header, infd);
    if (res)
    { fatality("failed to read header: %s.", comux_parse_result_string(res)); }

    // if the index is out of bounds, complain
    if (idx < 0 || idx >= manifest.header.num_chunks)
    {
        fatality("the chunk index must be between 0 and %d (inclusive)",
                 manifest.header.num_chunks - 1);
    }

    // iterate through the number of chunks specified in the header
    uint8_t match = 0;
    for (uint32_t i = 0; i < manifest.header.num_chunks && !match; i++)
    {
        // initialize and read the cinfo header
        comux_cinfo_t cinfo;
        comux_cinfo_init(&cinfo);
        res = comux_cinfo_read(&cinfo, infd);
        if (res)
        {
            fatality("failed to parse chunk %d: %s", i,
                     comux_parse_result_string(res));
        }

        // read the cinfo's data, then write it out if this is the index we're
        // looking for
        comux_cinfo_data_read(&cinfo, infd);
        if (i == idx)
        {
            // open an output file descriptor, write it out, then close it
            int outfd = io_out_get_fd();
            comux_cinfo_data_write(&cinfo, outfd);
            io_out_close_fd(outfd);
            match = 1;
        }

        // free the cinfo's memory
        comux_cinfo_free(&cinfo);
    }

    // close the file descriptor and free memory
    io_in_close_fd(infd);
    comux_manifest_free(&manifest, 0);
}

// Implements the -e / --edit-chunk functionality.
static void comux_edit_chunk(char* in)
{
    // attempt to parse the given argument as a chunk index
    long idx_converted;
    if (str_to_int(in, &idx_converted))
    { fatality("failed to parse a positive chunk index from \"%s\".", in); }
    uint32_t idx = (uint32_t) idx_converted;

    // if the settings weren't touched, complain
    if (!cid_touched && !scheduling_touched && !flags_touched)
    {
        fprintf(stderr, C_GRAY "Warning: no settings were adjusted. "
                "This function won't do anything.\n"
                "Try setting one of the --set-* fields "
                "(--set-conn, --set-sched, --set-flags) to use this.\n" C_NONE);
        return;
    }

    // open a file descriptor to read from, and write to
    int infd = io_in_get_fd();
    int outfd = io_out_get_fd();

    // make a manifest and parse the header, then write it back out
    comux_manifest_t manifest;
    comux_manifest_init(&manifest);
    comux_parse_result_t res = comux_header_read(&manifest.header, infd);
    if (res)
    { fatality("failed to read header: %s.", comux_parse_result_string(res)); }

    // if the index is out of bounds, complain
    if (idx < 0 || idx >= manifest.header.num_chunks)
    {
        fatality("the chunk index must be between 0 and %d (inclusive)",
                 manifest.header.num_chunks - 1);
    }

    comux_header_write(&manifest.header, outfd);

    // iterate through each chunk
    for (uint32_t i = 0; i < manifest.header.num_chunks; i++)
    {
        comux_cinfo_t cinfo;
        comux_cinfo_init(&cinfo);

        // parse the next chunk header
        res = comux_cinfo_read(&cinfo, infd);
        if (res)
        {
            fatality("failed to parse chunk %d: %s.", i,
                     comux_parse_result_string(res));
        }

        // if this is a match, adjust settings accordingly before writing out
        if (i == idx)
        {
            if (cid_touched)        { cinfo.id = cid; }
            if (scheduling_touched) { cinfo.sched = scheduling; }
            if (flags_touched)      { cinfo.flags = flags; }
        }

        // write the header, then read-and-write the data
        comux_cinfo_write(&cinfo, outfd);
        comux_cinfo_data_read(&cinfo, infd);
        comux_cinfo_data_write(&cinfo, outfd);

        // free memory
        comux_cinfo_free(&cinfo);
    }


    // close file descriptors and free memory
    io_out_close_fd(outfd);
    io_in_close_fd(infd);
    comux_manifest_free(&manifest, 0);
}

// Handles the -N / --set-num-conns functionality.
static void comux_set_num_conns(char* in)
{
    // first, try to parse the string as a number
    long nc_converted;
    if (str_to_int(in, &nc_converted))
    { fatality("failed to parse a positive integer from \"%s\".", in); }
    uint32_t nc = (uint32_t) nc_converted;

    // next, open a file descriptor to read from and write to
    int infd = io_in_get_fd();
    int outfd = io_out_get_fd();

    // read the manifest
    comux_manifest_t manifest;
    comux_manifest_init(&manifest);
    comux_parse_result_t res = comux_header_read(&manifest.header, infd);
    if (res)
    { fatality("failed to read header: %s.", comux_parse_result_string(res)); }

    // adjust 'num_conns' then write it back out
    manifest.header.num_conns = nc;
    comux_manifest_write(&manifest, outfd);

    // iterate through the number of chunks specified in the header
    for (uint32_t i = 0; i < manifest.header.num_chunks; i++)
    {
        // initialize and read the cinfo header, then write it out
        comux_cinfo_t cinfo;
        comux_cinfo_init(&cinfo);
        res = comux_cinfo_read(&cinfo, infd);
        if (res)
        {
            fatality("failed to read chunk %d: %s", i,
                     comux_parse_result_string(res));
        }
        comux_cinfo_write(&cinfo, outfd);

        // read the data, write the data, then free memory
        comux_cinfo_data_read(&cinfo, infd);
        comux_cinfo_data_write(&cinfo, outfd);
        comux_cinfo_free(&cinfo);
    }

    // close the file descriptor and free memory
    io_out_close_fd(outfd);
    io_in_close_fd(infd);
    comux_manifest_free(&manifest, 0);
}

// Main function.
int main(int argc, char** argv)
{
    // if not arguments were given, show the usage and exit
    if (argc == 1)
    {
        printf("The comux toolkit. Use this to read, create, and modify comux files.\n");
        usage(argv[0]);
        exit(EXIT_SUCCESS);
    }
    
    // wipe the outfile
    memset(outfile, 0, FILE_PATH_MAXLEN);
    char* action_arg = argv[0];

    // parse command-line options
    int val = 0;
    int index = 0;
    void (*action_func)(char*) = usage;
    while ((val = getopt_long(argc, argv, "sca:r:x:e:i:o:C:S:F:N:v", clopts, &index)) != -1)
    {
        switch (val)
        {
            case 's':
                action_func = comux_show;
                break;
            case 'c':
                action_func = comux_convert;
                break;
            case 'a':
                action_func = comux_add_chunk;
                action_arg = optarg;
                break;
            case 'r':
                action_func = comux_rm_chunk;
                action_arg = optarg;
                break;
            case 'x':
                action_func = comux_extract_chunk;
                action_arg = optarg;
                break;
            case 'e':
                action_func = comux_edit_chunk;
                action_arg = optarg;
                break;
            case 'i':
                snprintf(infile, FILE_PATH_MAXLEN, "%s", optarg);
                break;
            case 'o':
                snprintf(outfile, FILE_PATH_MAXLEN, "%s", optarg);
                break;
            case 'C':
                parse_conn_id(optarg);
                break;
            case 'S':
                parse_scheduling(optarg);
                break;
            case 'F':
                parse_flags(optarg);
                break;
            case 'N':
                action_func = comux_set_num_conns;
                action_arg = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
                break;
        }
    }

    // make a few verbose prints
    vprintf(stderr, C_GRAY "Comux Settings:\n");
    vprintf(stderr, C_GRAY "%-10s in=%s, out=%s\n" C_NONE, "I/O:",
            *infile == '\0' ? "stdin" : infile,
            *outfile == '\0' ? "stdout" : outfile);
    vprintf(stderr, C_GRAY "%-10s scheduling=%u, flags=0x%x\n\n"
            C_NONE, "Conn:", scheduling, flags);

    // call the action function
    action_func(action_arg);
}

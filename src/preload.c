// This C file implements the LD_PRELOAD portion of gurthang. It's responsible
// for overloading the accept() system call, parsing comux files, and creating
// connections to the target server that feed the comux file's data through it.
//
//      Connor Shugg

// Module inclusions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <signal.h>
// System call injection includes
#define __USE_GNU 1     // dlsym: required for RTLD_NEXT
#include <dlfcn.h>      // dlsym: required for function definition
// My includes
#include "comux/comux.h"
#include "utils/utils.h"
#include "utils/log.h"


// ========================== Globals and Macros =========================== //
#define GURTHANG_LIB // indicates that we're compiling the library
#define CHUNKS_MAX 1 << 13 // maximum number of chunks
#define PFX(name) __gurthang_lib_##name // to create library symbol names
#define COPFX "[C] " // prefix for controller thread logging
#define CHPFX "[CHK-%u] " // a prefix for each chunk thread
#define C_COPFX "\033[38;2;70;215;70m" // color for controller-thread prefix
#define C_CHPFX "\033[38;2;255;174;52m" // color for chunk-thread prefix
#define C_DATA "\033[38;2;150;150;225m" // used to log raw chunk data
#define C_NOTE "\033[38;2;100;125;255m" // used for special notes in the log
#define C_WARN "\033[38;2;200;100;0m" // used for warning log messages

// Logging, system call pointers, and other globals
#define GURTHANG_ENV_LIB_LOG "GURTHANG_LIB_LOG"
#define GURTHANG_ENV_LIB_NO_WAIT "GURTHANG_LIB_NO_WAIT"
static log_t log; // global log
static uint8_t wait_for_chunk_threads = 1; // disabled by _NO_WAIT env var
static int (*real_accept) (int, struct sockaddr*, socklen_t*);
static int (*real_accept4) (int, struct sockaddr*, socklen_t*, int);
static int(*real_listen) (int, int);
static int (*real_epoll_ctl) (int, int, int, struct epoll_event*);
static int (*real_epoll_wait) (int, struct epoll_event*, int, int);
static int accept_sock; // the server's connection-accepting socket
static int controller_initialized = 0;

// Send/receive tuning
#define GURTHANG_ENV_LIB_SEND_BUFFSIZE "GURTHANG_LIB_SEND_BUFFSIZE"
static size_t chunk_thread_write_buffsize = 2048; // default send() piece size
static const size_t chunk_thread_write_max_buffsize = 1 << 19;
#define GURTHANG_ENV_LIB_RECV_BUFFSIZE "GURTHANG_LIB_RECV_BUFFSIZE"
static size_t chunk_thread_read_buffsize = 2048; // default recv() buffer size
static const size_t chunk_thread_read_max_buffsize = 1 << 19;

// Exit tuning
#define GURTHANG_ENV_LIB_EXIT_IMMEDIATE "GURTHANG_LIB_EXIT_IMMEDIATE"
static uint8_t exit_immediate = 0;

// Servers using epoll to monitor the listener socket
static int epoll_fd = -1; // epoll FD tied to the accept socket FD

// Chunk thread locals
static __thread uint32_t chunk_thread_id = 0; // for chunk thread logging
static __thread uint8_t chunk_thread_is_final = 0; // for a conn's final chunk

// Thread synchronization
static pthread_mutex_t alock = PTHREAD_MUTEX_INITIALIZER; // for accept() calls


// ===================== Active Connection Management ====================== //
// This enum defines a series of status codes used to identify the current
// status of a single entry within our 'ctable' (connection table).
typedef enum connection_table_entry_status
{
    CONN_STATUS_DEAD = 0,           // no connection exists
    CONN_STATUS_ALIVE = 1,          // a connection is active
    CONN_STATUS_CLOSED_REMOTE = 2,  // the target server closed the connection
} ctable_status_t;

// Simple struct that contains an integer, representing a socket file
// descriptor. Used to store a list of currently-active connections to the
// target server (returned by chunk threads).
typedef struct connection_table_entry
{
    int fd;                     // the connection file descriptor
    ctable_status_t status;     // the status of this entry
} ctable_entry_t;

// The active-connection table. A simple array that, given a connection ID from
// a comux chunk header (0, 1, 2, etc.), one can look up the currently-opened
// file descriptor for that particular connection.
#define CTABLE_MAXLEN 1 << 12                   // maximum entries
static ctable_entry_t ctable[CTABLE_MAXLEN];    // global table of connections
static pthread_mutex_t ctable_lock;             // table lock


// ============================== Chunk Thead ============================== //
// Used to pass in all the necessary data for a single chunk thread.
typedef struct chunk_thread_parameters
{
    comux_cinfo_t* cinfo;       // the comux chunk it's responsible for
    uint32_t thread_id;         // id number (index) of the chunk thread
    uint8_t is_final_chunk;     // '1' if this is the last chunk for the
                                // connection ctable[cinfo->id], '0' if not
} chunk_thread_params_t;

// Takes in the necessary fields and returns a heap-allocated chunk thread
// parameter struct. Invoked when spawning a new chunk thread.
chunk_thread_params_t* PFX(chunk_thread_params_make)(comux_cinfo_t* cinfo,
                                                     uint32_t id,
                                                     uint8_t is_final)
{
    // allocate memory, assign fields, and return
    chunk_thread_params_t* result = alloc_check(sizeof(chunk_thread_params_t));
    result->cinfo = cinfo;
    result->thread_id = id;
    result->is_final_chunk = is_final;
    return result;
}

// Helper function for logging with chunk threads
#define chunk_log(log, format, ...) do                          \
    {                                                           \
        log_write((log_t*) log,                                 \
                  "%s" CHPFX "%s" format,                       \
                  LOG_NOT_USING_FILE(log) ? C_CHPFX : "",       \
                  chunk_thread_id,                              \
                  LOG_NOT_USING_FILE(log) ? C_NONE : ""         \
                  __VA_OPT__(,) __VA_ARGS__);                   \
    }                                                           \
    while (0)

// Does one of the following
//  1. Creates a new connection with the global accepting socket (if one isn't
//     present in the connection table for the given connection ID) and adds
//     it to the connection table.
//  2. Retrieves the existing connection socket FD from the connection table
//     (if one is already established)
// The file descriptor of the connection is returned regardless. -1 is returned
// on error.
static int PFX(chunk_get_connection)(uint32_t cid)
{
    pthread_mutex_lock(&ctable_lock);

    // take a look at the connection table and see if we can use an existing fd
    ctable_entry_t* entry = &ctable[cid];
    switch (entry->status)
    {
        // CASE 1: connection was created previously and is still alive
        case CONN_STATUS_ALIVE:
            // log a message, unlock, and return the valid file descriptor
            chunk_log(&log, "found existing socket FD for connection %u: %d",
                    cid, entry->fd);
            pthread_mutex_unlock(&ctable_lock);
            return entry->fd;
        // CASE 2: connection was previously closed by the server
        case CONN_STATUS_CLOSED_REMOTE:
            chunk_log(&log, "%sSKIP:%s existing socket FD for connection %u "
                      "(%d) was previously closed by the target server.",
                      LOG_NOT_USING_FILE(&log) ? C_WARN : "",
                      LOG_NOT_USING_FILE(&log) ? C_NONE : "",
                      cid, entry->fd);

            // attempt to exit the thread - since the server closed this
            // connection, the data this chunk thread wants to send no longer
            // has a place to go. So we're done with this thread.
            pthread_mutex_unlock(&ctable_lock);
            pthread_exit(NULL);
            break;
        // DEFAULT CASE: proceed to the code below to make a new socket
        default:
            break;
    }

    // otherwise we'll need to establish a new connection. First, we'll invoke
    // 'getsockname' to get the correct server address/port to connect to
    struct sockaddr_storage server_addr;
    socklen_t server_addr_len = sizeof(struct sockaddr_storage);
    if (getsockname(accept_sock, (struct sockaddr*) &server_addr, &server_addr_len) == -1)
    { fatality_errno(errno, "failed to getsockname()"); }

    // create a new socket with the server's address family
    int sockfd = -1;
    if ((sockfd = socket(server_addr.ss_family, SOCK_STREAM, 0)) == -1)
    { fatality_errno(errno, "failed to create a socket"); }

    // attempt to connect
    if (connect(sockfd, (const struct sockaddr*) &server_addr, server_addr_len) == -1)
    { fatality_errno(errno, "failed to connect to target server"); }

    // finally, add this new socket FD to the connection table
    ctable[cid].fd = sockfd;
    ctable[cid].status = CONN_STATUS_ALIVE;
    chunk_log(&log, "created new socket FD for connection %u: %d", cid, sockfd);

    pthread_mutex_unlock(&ctable_lock);
    return sockfd;
}

// Function responsible for seeking to the correct spot in stdin and reading
// the comux chunk's data segment into memory.
// Returns the number of bytes read (return value of 'comux_cinfo_data_read')
static size_t PFX(chunk_load_data)(comux_cinfo_t* cinfo)
{
    // attempt to seek back to the correct offset within stdin
    if (lseek(STDIN_FILENO, comux_cinfo_data_offset(cinfo), SEEK_SET) == -1)
    {
        fatality_errno(errno, "failed to seek to offset %d of chunk data segment",
                       cinfo->offset);
    }

    // attempt to read bytes, then log a message
    size_t rcount = comux_cinfo_data_read(cinfo, STDIN_FILENO);
    chunk_log(&log, "read %lu bytes for the chunk data segment:\n%s%s%s\n",
              rcount,
              LOG_NOT_USING_FILE(&log) ? C_DATA : "",
              buffer_dptr(&cinfo->data),
              LOG_NOT_USING_FILE(&log) ? C_NONE : "");
    return rcount;
}

// Helper routine that sends the comux chunk's data across the socket
// connection to the target server.
// Returns the number of bytes sent to the server.
static size_t PFX(chunk_send_data)(comux_cinfo_t* cinfo, int sockfd)
{
    // get a pointer to the chunk's data and setup looping variables
    char* dptr = buffer_dptr(&cinfo->data);
    size_t interval_size = cinfo->len > chunk_thread_write_buffsize ?
                           chunk_thread_write_buffsize : cinfo->len;
    ssize_t wcount = 0;
    size_t total_wcount = 0;

    // repeatedly invoke send() until all bytes from the chunk's data segment
    // have been sent to the target server.
    // We use MSG_NOSIGNAL as a flag to prevent it from generating the SIGPIPE
    // signal if the server closes the connection. This allows us to
    while ((wcount = send(sockfd, dptr + wcount,
                          MIN(cinfo->len - total_wcount, interval_size),
                          MSG_NOSIGNAL)) > 0)
    { total_wcount += wcount; }

    // check for the target server closing the connection
    if (wcount == -1)
    {
        if (errno == EPIPE || errno == ECONNRESET)
        {
            chunk_log(&log, "target server closed the connection (%s).",
                      strerror(errno));

            // update the connection's status in our table, for future threads
            // (and close the file descriptor)
            pthread_mutex_lock(&ctable_lock);
            ctable[cinfo->id].status = CONN_STATUS_CLOSED_REMOTE;
            pthread_mutex_unlock(&ctable_lock);
            close(sockfd);
            return 0;
        }
        else
        { fatality_errno(errno, "failed to send bytes to target server"); }
    }
    
    // report the number of bytes sent, and handle the case where this chunk is
    // the final one for the current connection
    chunk_log(&log, "sent %lu bytes through connection %u", total_wcount, cinfo->id);
    if (chunk_thread_is_final && !(cinfo->flags & COMUX_CHUNK_FLAGS_NO_SHUTDOWN))
    {
        // if this chunk thread is sending the final data for this particular
        // connection, then we're done with the write-end of this socket. (So,
        // we'll shut it down)
        if (shutdown(sockfd, SHUT_WR) == -1)
        { fatality_errno(errno, "failed to shutdown socket's write-end"); }

        // notify the user
        chunk_log(&log, "%sFINAL:%s closed socket's write-end",
                  LOG_NOT_USING_FILE(&log) ? C_NOTE : "",
                  LOG_NOT_USING_FILE(&log) ? C_NONE : "");
    }

    return total_wcount;
}

// Attempts to recv() bytes from the target server on the active connection
// specified by the given socket file descriptor. The response is printed to
// STDOUT, and the number of bytes read is returned.
static size_t PFX(chunk_recv_data)(comux_cinfo_t* cinfo, int sockfd)
{
    // first, we'll set up a buffer to hold the server's response
    size_t buff_len = chunk_thread_read_buffsize;
    char buff[buff_len];

    chunk_log(&log, "receiving bytes from connection %u (to stdout)",
              cinfo->id);

    // repteatedly receive bytes from the server until some error occurs or
    // we run out of bytes
    ssize_t rcount = 0;
    size_t total_rcount = 0;
    while ((rcount = recv(sockfd, buff, buff_len, 0)) > 0)
    {
        total_rcount += rcount;

        // dump all the bytes to stdout
        ssize_t wcount = 0;
        size_t total_wcount = 0;
        while ((wcount = write(STDOUT_FILENO, buff + total_wcount,
                               rcount - total_wcount)) > 0)
        { total_wcount += wcount; }

        // check for failure to write to stdout
        if (wcount == -1)
        { fatality_errno(errno, "failed to write bytes to stdout"); }
    }
    if (total_rcount > 0)
    { write(STDOUT_FILENO, "\n", 1); }

    // check for error - if we get a specific error(s) (such as ECONNRESET),
    // we don't need to exit. Just move on and close the socket
    int8_t do_conn_close = total_rcount == 0;
    if (rcount == -1)
    {
        if (errno == ECONNRESET)
        { do_conn_close = -1; }
        else
        { fatality_errno(errno, "failed to read bytes from target server"); }
    }
    
    // if it's decided we need to close the connection, do so
    if (do_conn_close)
    {
        // print according to *what* caused the connection to be closed
        if (do_conn_close == -1)
        {
            chunk_log(&log, "target server closed the connection. (%s)",
                      strerror(errno));
        }
        else
        { chunk_log(&log, "target server closed the connection."); }

        // update the connection's status in our table, so any future threads
        // working with this socket are aware. Then, close the FD
        pthread_mutex_lock(&ctable_lock);
        ctable[cinfo->id].status = CONN_STATUS_CLOSED_REMOTE;
        pthread_mutex_unlock(&ctable_lock);
        close(sockfd);
        return total_rcount;
    }

    chunk_log(&log, "received %lu bytes from connection %u",
              total_rcount, cinfo->id);
    return total_rcount;
}

// The main function for each chunk thread. Chunk threads take in a pointer to
// a heap-allocated comux_cinfo_t struct, and use it to 
static void* PFX(chunk_main)(void* input)
{
    // retrieve needed fields from the parameter struct, then free it
    chunk_thread_params_t* params = (chunk_thread_params_t*) input;
    comux_cinfo_t* cinfo = params->cinfo;
    chunk_thread_is_final = params->is_final_chunk;
    chunk_thread_id = params->thread_id;
    free(params);

    chunk_log(&log, "spawned to handle chunk with fields: "
              "conn_id=%u, datalen=%lu, sched=%u, flags=0x%x.",
              cinfo->id, cinfo->len, cinfo->sched, cinfo->flags);

    // first, try to get an active connection with the server, based on the
    // chunk's connection ID
    int fd = PFX(chunk_get_connection)(cinfo->id);
    if (fd == -1)
    {
        fatality("failed to get an active connection for connection %u",
                 cinfo->id);
    }

    // next, read the chunk's data bytes from stdin
    size_t data_length = PFX(chunk_load_data)(cinfo);
    if (data_length == 0)
    {
        fatality("read zero bytes from a chunk data segment."
                 "Please check your input file.");
    }

    // send the data bytes over to the target server. If the connection was
    // closed, go no further
    if (PFX(chunk_send_data)(cinfo, fd) == 0)
    {
        comux_cinfo_free(cinfo);
        return NULL;
    }

    // if specified by the chunk's header data, wait for the server's response
    if (cinfo->flags & COMUX_CHUNK_FLAGS_AWAIT_RESPONSE)
    { PFX(chunk_recv_data)(cinfo, fd); }

    comux_cinfo_free(cinfo);
    return NULL;
}


// =========================== Controller Thead ============================ //
// Helper function for logging with chunk threads
#define ctl_log(log, format, ...) do                            \
    {                                                           \
        log_write((log_t*) log,                                 \
                  "%s" COPFX "%s" format,                       \
                  LOG_NOT_USING_FILE(log) ? C_COPFX : "",       \
                  LOG_NOT_USING_FILE(log) ? C_NONE : ""         \
                  __VA_OPT__(,) __VA_ARGS__);                   \
    }                                                           \
    while (0)

// Helper function used to have the controller thread exit/kill the entire
// process.
static void PFX(controller_exit)()
{
    if (exit_immediate)
    { _exit(EXIT_SUCCESS); }
    else
    { exit(EXIT_SUCCESS); }
}

// The main function for the main library thread.
static void* PFX(controller_main)(void* input)
{
    ctl_log(&log, "controller thread spawned. Reading from stdin...");

    // the first thing we'll do is read the comux header from stdin
    comux_manifest_t manifest;
    comux_manifest_init(&manifest);
    comux_parse_result_t res = comux_header_read(&manifest.header, STDIN_FILENO);
    if (res)
    {
        fatality("failed to parse comux header: %s",
                 comux_parse_result_string(res));
    }
    ctl_log(&log, STAB_TREE2 "found comux formatting with "
            "%u connection(s) and %u chunk(s).",
            manifest.header.num_conns, manifest.header.num_chunks);
    
    // if the number of connections within the file exceeds our maximum,
    // complain and exit
    if (manifest.header.num_conns > CTABLE_MAXLEN)
    {
        fatality("the given comux file exceeds the maximum number of connections (%u)",
                 CTABLE_MAXLEN);
    }
    
    // grab the number of chunks and make sure it doesn't exceed our maximum
    // value. If it's all good, wipe the manifest field (since it'll be
    // incremented in the loop below)
    uint32_t num_chunks = manifest.header.num_chunks;
    if (num_chunks > CHUNKS_MAX)
    {
        fatality("the given comux file exceeds the maximum number of chunks (%u)",
                 CHUNKS_MAX);
    }
    manifest.header.num_chunks = 0;

    // make a temporary buffer to determine whether or not ALL the connections
    // specified in the comux header have at least one chunk assigned to them
    uint8_t ccheck_buff[manifest.header.num_conns];
    memset(ccheck_buff, 0, manifest.header.num_conns);

    // next, we'll read each chunk header
    comux_cinfo_t* chunks[num_chunks];
    for (uint32_t i = 0; i < num_chunks; i++)
    {
        // because these chunk structs will be passed to a new thread, we
        // need them to be placed on the heap
        chunks[i] = alloc_check(sizeof(comux_cinfo_t));
        comux_cinfo_t* current = chunks[i];
        comux_cinfo_init(current);
        
        // read the cinfo's header information
        if ((res = comux_cinfo_read(current, STDIN_FILENO)))
        {
            fatality("failed to parse comux chunk %u: %s",
                     i + 1, comux_parse_result_string(res));
        }

        // write a message out to the log describing what we found
        ctl_log(&log, "%sfound chunk %u with fields: "
                "conn_id=%u, datalen=%lu, sched=%u, flags=0x%x.",
                i < num_chunks - 1 ? STAB_TREE2 : STAB_TREE1, i,
                current->id, current->len, current->sched, current->flags);
        
        // make sure the connection ID is within the range specified by the
        // header's 'num_conns'
        if (current->id >= manifest.header.num_conns)
        {
            fatality("Chunk %u has a connection ID (%u) outside the range of "
                     "specified connections: [0, %u]",
                     i, current->id, manifest.header.num_conns);
        }
        ccheck_buff[current->id]++;

        // add this cinfo struct to the manifest
        comux_manifest_cinfo_add(&manifest, current);
        
        // for now, we won't read the data for each chunk. We'll seek past it
        // to get to the next connection header
        if (lseek(STDIN_FILENO, current->len, SEEK_CUR) == -1)
        { fatality_errno(errno, "failed to seek past chunk %u's data segment", i + 1); }
    }

    // pass through the buffer we made earlier to determine if there are any
    // connections that don't have ANY chunks
    for (uint32_t i = 0; i < manifest.header.num_conns; i++)
    {
        if (ccheck_buff[i] == 0)
        { fatality("connection ID %u is assigned zero chunks in this file.", i); }
    }

    // set up a buffer to hold all the chunk threads' pthread IDs
    pthread_t chunk_tids[manifest.header.num_chunks];
    memset(chunk_tids, 0, manifest.header.num_chunks);

    // at this point we've parsed the comux header and all the chunk headers.
    // Now, we need to start the process of spawning a thread for each
    // connection. We'll do this in scheduling-value-order (that is, comux
    // connections with LOWER 'sched' fields will go first).
    uint32_t idx = 0;
    while (manifest.cinfo_list.size > 0)
    {
        // by default, we'll select the first entry in the list
        dllist_elem_t* e = dllist_get_head(&manifest.cinfo_list);
        comux_cinfo_t* next = e->container;

        // iterate through the list and find the lowest possible scheduling
        // value. We'll remove and handle this one next
        dllist_iterate(&manifest.cinfo_list, e)
        {
            comux_cinfo_t* cinfo = e->container;
            // if this scheduling value is lower than our current scheduling
            // value, update it
            if (cinfo->sched < next->sched)
            { next = cinfo; }
        }

        // remove the selected entry from the list and decrement our
        // connection-checking buffer (used above) so we know exactly how many
        // chunks remain that deal with each connection. (By knowing this,
        // we can tell the next chunk thread whether or not it's the LAST
        // thread dealing with a specific connection)
        dllist_remove(&manifest.cinfo_list, &next->elem);
        uint8_t is_final_conn_chunk = --ccheck_buff[next->id] == 0;

        // create a new parameter struct to pass to the chunk thread
        chunk_thread_params_t* params = PFX(chunk_thread_params_make)(next,
                                            idx, is_final_conn_chunk);

        // at this point, we'll spawn a new "chunk thread" to handle this
        // particular chunk
        ctl_log(&log, "spawning chunk thread %u.%s%s%s", idx,
                LOG_NOT_USING_FILE(&log) ? C_WARN : "",
                wait_for_chunk_threads ? "" : " NO_WAIT mode enabled.",
                LOG_NOT_USING_FILE(&log) ? C_NONE : "");
        int err = pthread_create(&chunk_tids[idx], NULL, PFX(chunk_main), params);
        if (err)
        { fatality_errno(err, "failed to spawn chunk thread %u", idx); }

        // join the thread (only if NO_WAIT mode is disabled)
        if (wait_for_chunk_threads)
        {
            void* retval = NULL;
            int join_err = pthread_join(chunk_tids[idx], &retval);
            if (join_err)
            { fatality_errno(join_err, "failed to join chunk thread %u", idx); }
            ctl_log(&log, "joined chunk thread %u.", idx);
        }

        idx++;
    }

    // if NO_WAIT mode is enabled, we'll spawn all the threads (done above),
    // NOT wait for each one sequentially, then wait for all of them right here
    if (!wait_for_chunk_threads)
    {
        ctl_log(&log, "%sNO_WAIT:%s all chunk threads spawned. Joining...",
                LOG_NOT_USING_FILE(&log) ? C_WARN : "",
                LOG_NOT_USING_FILE(&log) ? C_NONE : "");
        // iterate through all chunk threads and join them
        for (uint32_t i = 0; i < manifest.header.num_chunks; i++)
        {
            void* retval = NULL;
            int join_err = pthread_join(chunk_tids[i], &retval);
            if (join_err)
            { fatality_errno(join_err, "failed to join chunk thread %u", i); }
            ctl_log(&log, "%s%sNO_WAIT:%s joined chunk thread %u.",
                    i < manifest.header.num_chunks - 1 ? STAB_TREE2 : STAB_TREE1,
                    LOG_NOT_USING_FILE(&log) ? C_WARN : "",
                    LOG_NOT_USING_FILE(&log) ? C_NONE : "",
                    i);
        }
    }

    // free chunk memory
    for (uint32_t i = 0; i < num_chunks; i++)
    {
        comux_cinfo_free(chunks[i]);
    }

    // print an exit message and return
    ctl_log(&log, "exiting.");
    PFX(controller_exit)();
    return NULL;
}

// Spawns the main library thread (the "controller" thread). This thread is
// responsible for reading comux-formatted input from stdin and spawning
// threads for each chunk specified within the comux file.
// If spawning the thread fails, the program exits.
static void PFX(controller_spawn)()
{
    // attempt to spawn the thread
    pthread_t tid;
    int err = pthread_create(&tid, NULL, PFX(controller_main), NULL);
    if (err)
    { fatality_errno(err, "failed to spawn main library thread"); }

    // detach the controller thread - nobody will be calling pthread_join() on
    // it in the future, so we need the OS to clean its resources up itself
    err = pthread_detach(tid);
    if (err)
    {
        fatality_errno(err, "failed to detacth main library thread: %s",
                       strerror(err));
    }
}


// =============== Initialization and System Call Injection ================ //
// Helper function used during the initialization process that attempts to
// parse environment variables.
static void PFX(init_environment_variables)()
{
    // set up a small array of environment variables that take in unsigned ints
    // and their respective global fields
    char* unsigned_int_envvars[2] = {
        GURTHANG_ENV_LIB_SEND_BUFFSIZE,
        GURTHANG_ENV_LIB_RECV_BUFFSIZE
    };
    unsigned long* unsigned_int_envvars_fields[2] = {
        &chunk_thread_write_buffsize,
        &chunk_thread_read_buffsize
    };
    unsigned long unsigned_int_envvars_maximums[2] = {
        chunk_thread_write_max_buffsize,
        chunk_thread_read_max_buffsize
    };

    for (int i = 0; i < sizeof(unsigned_int_envvars) / sizeof(char*); i++)
    {
        char* env_name = unsigned_int_envvars[i];
        unsigned long* env_field = unsigned_int_envvars_fields[i];
        unsigned long env_maximum = unsigned_int_envvars_maximums[i];

        // attempt to retrieve the environment variable's value
        char* env = getenv(env_name);
        if (!env)
        { continue; }

        // write a message stating we found it
        log_write(&log, "found %s%s=%s%s.",
                LOG_NOT_USING_FILE(&log) ? C_DATA : "",
                env_name, env,
                LOG_NOT_USING_FILE(&log) ? C_NONE : "");
        
        // attempt to convert the integer
        long conversion = 0;
        if (str_to_int(env, &conversion) || conversion <= 0)
        {
            fatality("%s%s%s must be set to a positive integer.",
                    LOG_NOT_USING_FILE(&log) ? C_DATA : "",
                    env_name,
                    LOG_NOT_USING_FILE(&log) ? C_NONE : "");
        }

        // there's a cap, enforce it if necessary
        if (env_maximum > 0 && conversion > env_maximum)
        {
            conversion = env_maximum;
            log_write(&log, STAB_TREE1 "exceeded maximum value - capped off at %lu.",
                      env_maximum);
        }

        // save the integer
        *env_field = conversion;
    }

    // look for the 'NO_WAIT' environment variable. If this is defined, we'll
    // flip a global switch that forces the controller thread to NOT wait for
    // chunk threads as they're spawned. Instead, the controller thread will
    // spawn all N chunk threads, *then* wait for all N of them to exit.
    if (getenv(GURTHANG_ENV_LIB_NO_WAIT))
    {
        log_write(&log, "found %s%s%s. Enabling %sNO_WAIT%s mode.",
                  LOG_NOT_USING_FILE(&log) ? C_DATA : "",
                  GURTHANG_ENV_LIB_NO_WAIT,
                  LOG_NOT_USING_FILE(&log) ? C_NONE : "",
                  LOG_NOT_USING_FILE(&log) ? C_WARN : "",
                  LOG_NOT_USING_FILE(&log) ? C_NONE : "");
        wait_for_chunk_threads = 0;
    }

    // look for the 'EXIT_IMMEDIATE' environment variable. If this is set, the
    // controller thread will invoke _exit() rather than exit(), to avoid the
    // controller thread running any registered exit handlers, which may cause
    // issues.
    if (getenv(GURTHANG_ENV_LIB_EXIT_IMMEDIATE))
    {
        log_write(&log, "found %s%s%s. The controller thread will invoke "
                  "_exit(), rather than exit().",
                  LOG_NOT_USING_FILE(&log) ? C_DATA : "",
                  GURTHANG_ENV_LIB_EXIT_IMMEDIATE,
                  LOG_NOT_USING_FILE(&log) ? C_NONE: "");
        exit_immediate = 1;
        fatality_set_exit_method(1); // make sure we _exit() on fatal errors
    }
}

// The initialization function for the library. Called a single time by the
// first thread that calls accept(). Takes in the socket file descriptor the
// target server is accepting connections on.
static void PFX(init)(int sockfd)
{
    log_init(&log, "gurthang-lib", GURTHANG_ENV_LIB_LOG);

    // check other environment variables
    PFX(init_environment_variables)();

    // save the socket file descriptor
    accept_sock = sockfd;

    // look up the real accept() system call, so we can invoke it at the end of
    // our own injected version of accept(). Exit on failure
    real_accept = dlsym(RTLD_NEXT, "accept");
    if (!real_accept)
    { fatality("failed to look up 'accept' system call"); }
    else
    { log_write(&log, "found accept system call: %p", real_accept); }
    
    // look up the real accept4() system call in the same manner
    real_accept4 = dlsym(RTLD_NEXT, "accept4");
    if (!real_accept)
    { fatality("failed to look up 'accept4' system call"); }
    else
    { log_write(&log, "found accept4 system call: %p", real_accept4); }

    // look up the real listen() system call in the same manner
    real_listen = dlsym(RTLD_NEXT, "listen");
    if (!real_listen)
    { fatality("failed to look up 'listen' system call"); }
    else
    { log_write(&log, "found listen system call: %p", real_listen); }
    
    // look up the real epoll_ctl()
    real_epoll_ctl = dlsym(RTLD_NEXT, "epoll_ctl");
    if (!real_epoll_ctl)
    { fatality("failed to look up 'epoll_ctl' system call"); }
    else
    { log_write(&log, "found epoll_ctl system call: %p", real_epoll_ctl); }
    
    // look up the real epoll_wait()
    real_epoll_wait = dlsym(RTLD_NEXT, "epoll_wait");
    if (!real_epoll_wait)
    { fatality("failed to look up 'epoll_wait' system call"); }
    else
    { log_write(&log, "found epoll_wait system call: %p", real_epoll_wait); }

    // initialize the connection table (we don't have any live connections yet)
    for (uint32_t i = 0; i < CTABLE_MAXLEN; i++)
    { ctable[i].status = CONN_STATUS_DEAD; }
    pthread_mutex_init(&ctable_lock, NULL);
}

// ========================= System Call Injection ========================= //
// This overloads the listen() system call. We use this simply to capture the
// server's listener socket for later use.
int listen(int sockfd, int backlog)
{
    pthread_mutex_lock(&alock);
    // we'll use 'listen_count' to ensure we don't initialize multiple times
    static uint8_t listen_count = 0;
    if (!listen_count)
    {
        PFX(init)(sockfd);
        listen_count++;
    }
    pthread_mutex_unlock(&alock);

    // invoke the REAL listen() system call
    return real_listen(sockfd, backlog);
}

// Overloads epoll_ctl() system call. We overload this in order to detect and
// save the file descriptor used with the server's listener socket file
// descriptor (if the server does this).
int epoll_ctl(int epfd, int op, int fd, struct epoll_event* event)
{
    // only perform extra actions if we haven't already set the epoll FD
    if (epoll_fd == -1)
    {
        pthread_mutex_lock(&alock);
        // if the accept socket hasn't been saved yet (via our injected
        // listen() system call), log this (this might happen if an unrelated
        // epoll set is created before listen() is invoked)
        if (accept_sock == -1)
        {
            log_write(&log, C_WARN "epoll_ctl() invoked before the listener "
                      "socket was discovered." C_NONE);
        }
        // otherwise, check to see if this epoll set is having the listener
        // socket added to it. If that's the case, we're interested
        else if (op == EPOLL_CTL_ADD && accept_sock == fd)
        {
            epoll_fd = epfd;
            log_write(&log, "found listener socket epoll FD: %d", epfd);
        }
        pthread_mutex_unlock(&alock);
    }

    // call the original epoll_ctl()
    return real_epoll_ctl(epfd, op, fd, event);
}

// Overloads epoll_wait() system call. We overload this in the event the server
// has an epoll file descriptor used to monitor the listener socket. A thread
// for such a server might call epoll_wait() before it calls accept(). If this
// library simply blocked on accept(), it would never know the threads are
// stuck in epoll_wait() waiting for a connection to be made.
int epoll_wait(int epfd, struct epoll_event* events, int maxevents, int timeout)
{
    pthread_mutex_lock(&alock);

    // if we saved an epoll file descriptor in an earlier call to epoll_ctl(),
    // we'll proceed so long as this call to epoll_wait() is using that FD.
    // Also, we'll only run the below code if the controller thread also hasn't
    // been initialized yet.
    uint8_t waiting_on_listener_epfd = epoll_fd > -1 && epoll_fd == epfd;
    if (waiting_on_listener_epfd && !controller_initialized)
    {
        log_write(&log, "spawning controller thread (via epoll_wait).");
        PFX(controller_spawn)();        // spawn the controller thread
        controller_initialized++;       // make sure this doesn't happen again
    }

    // invoke and return the real epoll_wait()
    pthread_mutex_unlock(&alock);
    return real_epoll_wait(epfd, events, maxevents, timeout);
}

// The definition for our own version of the accept() system call. The target
// server's thread will call this, run some extra library code, then make a
// call to the REAL accept() and return its value.
int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
    pthread_mutex_lock(&alock);

    // initialize the library, if we haven't done so already
    if (!controller_initialized)
    {
        log_write(&log, "spawning controller thread (via accept).");
        PFX(controller_spawn)();        // spawn the controller thread
        controller_initialized++;       // make sure this doesn't happen again
    }

    // finally, release the lock and invoke the REAL system call
    pthread_mutex_unlock(&alock);
    return real_accept(sockfd, addr, addrlen);
}

// Definition of our own version of accept4(). Behaves exactly the same as our
// version of accept(). Initializes the controller thread if applicable, then
// makes a call to the REAL accept4().
int accept4(int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags)
{
    pthread_mutex_lock(&alock);

    // initialize the library if it has yet to be done
    if (!controller_initialized)
    {
        log_write(&log, "spawning controller thread (via accept4).");
        PFX(controller_spawn)();        // spawn controller thread
        controller_initialized++;       // make sure it doesn't happen again
    }

    // unlock and invoke the real accept4()
    pthread_mutex_unlock(&alock);
    return real_accept4(sockfd, addr, addrlen, flags);
}

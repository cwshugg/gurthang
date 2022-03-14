// Implements the functions defined in log.h.
//
//      Connor Shugg

// Module inclusions
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include "utils.h"
#include "log.h"

#define LOG_C_PREFIX "\033[90m"
#define LOG_C_TEXT "\033[0m"
#define LOG_C_NONE "\033[0m"

// Threading
static pthread_t make_log_id(pthread_t tid)
{ return (tid >> 31) + (tid % 1000); }


// ============================ The Log Struct ============================= //
void log_init(log_t* l, char* prefix, char* envar)
{
    // copy the given prefix and collect the current time
    snprintf(l->prefix, GURTHANG_LOG_PREFIX_MAXLEN, "%s", prefix);
    l->fpath = NULL;
    l->time_init = time(NULL);

    // check the environment variable - if it's present, we'll try to parse it
    char* logfile = getenv(envar);
    if (!logfile)
    { return; }

    // initialize the log's mutex lock
    if (pthread_mutex_init(&l->lock, NULL))
    { fatality_errno(errno, "failed to initialize log mutex lock"); }

    // check the value for a file name, stdout, or stderr
    size_t len = strlen(logfile);
    if (*logfile == '1' && len == 1)        // CASE 1: log to stdout
    {
        l->fpath = (char*) LOG_STDOUT_INDICATOR;
        log_write(l, "logging enabled to stdout.");
    }
    else if (*logfile == '2' && len == 1)   // CASE 2: log to stderr
    {
        l->fpath = (char*) LOG_STDERR_INDICATOR;
        log_write(l, "logging enabled to stderr.");
    }
    else                                // CASE 3: log to a file
    {
        // assume we were given a file name and save it to our struct
        l->fpath = strdup(logfile);
        if (!l->fpath)
        { fatality_errno(errno, "failed to copy log file path: %s", logfile); }

        // attempt to remove the file before writing to it, if it exists
        if (!access(l->fpath, F_OK))
        {
            if (remove(l->fpath) == -1)
            { fatality_errno(errno, "failed to delete old log file: %s", logfile); }
        }

        log_write(l, "logging enabled to file: %s.", logfile);
    }
}

int log_write(log_t* l, char* format, ...)
{
    if (!l->fpath)
    { return 0; }
    
    // get the current time
    time_t time_curr = time(NULL);

    // first, create a buffer with the format string AND a prefix
    size_t buff_len = GURTHANG_LOG_MESSAGE_MAXLEN + GURTHANG_LOG_PREFIX_MAXLEN + 64;
    char modified_format[buff_len];

    // create a smaller buffer to hold a thread-ID string
    char thread_id[16];
    snprintf(thread_id, 16, "(T-%ld)", make_log_id(pthread_self()));

    // come up with the formatter string, using colors only if the log is
    // writing to stdout or stderr
    snprintf(modified_format, buff_len,
             LOG_NOT_USING_FILE(l) ?
             LOG_C_PREFIX "[%s %-10s %8lds] " LOG_C_TEXT "%s\n" LOG_C_NONE :
             "[%s %-10s %8lds] %s\n",
             l->prefix, thread_id, time_curr - l->time_init, format);
    
    // acquire the log's lock
    int err = pthread_mutex_lock(&l->lock);
    if (err)
    { fatality_errno(err, "failed to acquire log lock"); }
    
    // determine the file pointer to use/open
    FILE* out = NULL;
    if (LOG_USING_STDOUT(l))
    { out = stdout; }
    else if (LOG_USING_STDERR(l))
    { out = stderr; }
    else
    {
        // attempt top open the file for appending
        out = fopen(l->fpath, "a");
        if (!out)
        {
            fatality_errno(errno, "failed to open log file for writing: %s",
                            l->fpath);
        }
    }

    // create a va_list and write to the output stream
    va_list vl;
    va_start(vl, format);
    int bytes_written = vfprintf(out, modified_format, vl);
    va_end(vl);

    // close the file stream, if applicable
    if (!LOG_NOT_USING_FILE(l))
    { fclose(out); }
    
    // release the log's lock
    err = pthread_mutex_unlock(&l->lock);
    if (err)
    { fatality_errno(err, "failed to release log lock"); }

    // return the number of bytes written
    return bytes_written;

}

void log_free(log_t* l)
{
    // free the path string, if applicable
    if (l->fpath && !LOG_NOT_USING_FILE(l))
    { free(l->fpath); }

    // NULL-out the file path string
    l->fpath = NULL;
}

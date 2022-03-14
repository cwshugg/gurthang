// This file defines data structures and function prototypes used by BOTH the
// mutator and the preload library to make verbose logging, enabled via an
// environment variable.
//
//      Connor Shugg

#if !defined(GURTHANG_LOG_H)
#define GURTHANG_LOG_H

// ============================ The Log Struct ============================= //
#define GURTHANG_LOG_PREFIX_MAXLEN 32 // max length of the prefix for the log
#define GURTHANG_LOG_MESSAGE_MAXLEN 512 // max length of a log message

// Macros used to determine where we're writing to
#define LOG_STDOUT_INDICATOR 0x1
#define LOG_STDERR_INDICATOR 0x2
#define LOG_USING_STDOUT(log) (((log_t*) log)->fpath == (char*) LOG_STDOUT_INDICATOR)
#define LOG_USING_STDERR(log) (((log_t*) log)->fpath == (char*) LOG_STDERR_INDICATOR)
#define LOG_NOT_USING_FILE(log) (LOG_USING_STDOUT(log) || LOG_USING_STDERR(log))

// This struct represents a log and all its needed fields. This can be
// initialized, then enabled, then written to, then de-initialized.
typedef struct log
{
    char* fpath;                // the file path to write to
    time_t time_init;           // the time the log was initialized
    char prefix[GURTHANG_LOG_PREFIX_MAXLEN]; // prefix string printed for log
    pthread_mutex_t lock;       // lock used with multiple threads
} log_t;

// Initializes the log struct, given the environment variable name to use for
// enabling logging. If the environment variable is found, the log's internal
// file pointer is initialized appropriately. If it's not, it's left as NULL.
void log_init(log_t* log, char* prefix, char* envar);

// A printf-like function that takes a log, format string, and va-args and
// writes to output. Returns the number of bytes written, or 0 if the
// internal file pointer is NULL.
int log_write(log_t* log, char* format, ...);

// Frees up any memory associated with the given log struct.
void log_free(log_t* log);

#endif

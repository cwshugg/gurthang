// This header file defines a few globals and defines for the mutator module.
//
//      Connor Shugg

#if !defined(GURTHANG_MUT)
#define GURTHANG_MUT

#include <stdlib.h>
#include "utils/log.h"

// Globals/defines
#define PFX(name) __gurthang_mut_##name // to create mutator symbol names
#define C_FUNC "\033[38;2;70;215;70m" // color used when logging function names
#define C_GOOD "\033[32m" // color used to log something good
#define C_BAD "\033[31m" // color used to log something bad

// Random-generation macros
#define RAND_UNDER(ceiling) (rand() % (ceiling))

// Comux-related globals/defines
#define MAX_CONNECTIONS 1 << 12     // maximum number of allowed connections
#define MAX_CHUNKS 1 << 13          // maximum number of allowed chunks


// ========================== Logging & Debugging ========================== //
// Helper macro for logging when debug-printing is enabled.
#define dlog_write(log, format, ...) do                         \
    {                                                           \
        if (!debug_log) { break; }                              \
        log_write((log_t*) log, format __VA_OPT__(,) __VA_ARGS__); \
    }                                                           \
    while (0)

// Helper macro for logging with the function name prepended to the message.
#define flog_write(log, format, ...) do                         \
    {                                                           \
        log_write((log_t*) log, "%s%s:%s " format,              \
                  LOG_NOT_USING_FILE(log) ? C_FUNC : "", __func__, \
                  LOG_NOT_USING_FILE(log) ? C_NONE : ""         \
                  __VA_OPT__(,) __VA_ARGS__);                   \
    }                                                           \
    while (0)

#endif

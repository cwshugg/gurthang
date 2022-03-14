// Defines the functions needed to plug this into AFL++ as a custom mutator.
//
// A few useful links/guides on AFL++ custom mutators:
//  - https://aflplus.plus/docs/custom_mutators/
//  - https://securitylab.github.com/research/fuzzing-software-2/
//
//      Connor Shugg

// Module inclusions
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include "comux/comux.h"
#include "utils/utils.h"
#include "utils/log.h"
#include "utils/dict.h"
#include "mutator.h"

// AFL++ inclusions
#include "config.h"
#include "types.h"
#include "afl-fuzz.h"
#include "custom_mutator_helpers.h"

// Memcheck defines/includes
//#define GURTHANG_MUT_MEMCHECK // define/undefine to compile in memcheck code
#if defined(GURTHANG_MUT_MEMCHECK)
#include <valgrind.h>
#include <memcheck.h>
#endif

// Globals and defines
#define GURTHANG_ENV_MUT_LOG "GURTHANG_MUT_LOG"
#define GURTHANG_ENV_MUT_DEBUG "GURTHANG_MUT_DEBUG"
log_t mlog; // global log
uint8_t debug_log = 0; // controlled by GURTHANG_ENV_MUT_DEBUG

// Fuzzing-related globals
#define GURTHANG_ENV_MUT_FUZZ_MIN "GURTHANG_MUT_FUZZ_MIN"
static uint32_t fuzz_min = 512; // min count of fuzzing attempts for an input
#define GURTHANG_ENV_MUT_FUZZ_MAX "GURTHANG_MUT_FUZZ_MAX"
static uint32_t fuzz_max = 32768; // max count of fuzzing attempts for an input

// Trimming-related globals
#define GURTHANG_ENV_MUT_TRIM_MAX "GURTHANG_MUT_TRIM_MAX"
static ssize_t trim_steps_max = 2500; // maximum number of trim steps

// Dictionary globals
#define GURTHANG_ENV_MUT_DICT "GURTHANG_MUT_DICT"
static const size_t max_dicts = 32; // maximum number of dictionaries allowed
static dllist_t dlist; // dictionary list
static uint8_t use_dicts = 0; // controlled by GURTHANG_ENV_MUT_DICT

// This, when defined, will define the two havoc-mutation functions:
//  1. afl_custom_havoc_mutation()
//  2. afl_custom_havoc_mutation_probability()
// And thus will cause AFL++ to invoke it when performing its havoc stage.
#define GURTHANG_MUT_HAVOC_SUPPORT


// ======================= Mutator Data Structure(s) ======================= //
// This enum is used to define various fuzzing strategies choosable by the
// mutator when fuzzing a given comux file.
typedef enum gurthang_mutate_strategy
{
    STRAT_CHUNK_DATA_HAVOC,     // random AFL-like fuzzing on chunk data
    STRAT_CHUNK_DATA_EXTRA,     // additional havoc-like fuzzing
    STRAT_CHUNK_SCHED_BUMP,     // try modifying a chunk's 'sched' field
    STRAT_CHUNK_SPLIT,          // split a chunk into two chunks
    STRAT_CHUNK_SPLICE,         // combine two chunks of the same connection
    STRAT_CHUNK_DICT_SWAP,      // swap a word in a dictionary for another
    // ----------------------
    STRAT_LENGTH,               // used to store the number of strategies
    // ----------------------
    STRAT_FIXUP,                // fix/remake a broken comux input
    STRAT_UNKNOWN               // used as an 'uninitialized' value
} gurthang_strategy_t;

// A single struct used to carry around all the metadata for this mutator.
typedef struct gurthang_mutator
{
    afl_state_t* afl;   // internal AFL++ state object pointer
    buffer_t buff;      // reusable, resizable buffer
    buffer_t dbuff;     // buffer holding a mutation description (afl_custom_describe)

    // Trimming fields
    buffer_t tbuff_head;    // buffer used to hold bytes BEFORE trim section
    buffer_t tbuff_tail;    // buffer used to hold bytes AFTER trim section
    buffer_t tbuff;         // buffer used to return the total trimmed comux
    comux_cinfo_t trim_cinfo; // chunk info for the selected chunk for trimming
    comux_cinfo_t trim_cinfo_old; // old chunk state prior to trimming step
    size_t trim_cinfo_old_size; // total byte size of the old comux chunk
    size_t trim_bytes_per_step; // number of bytes to remove per step
    int trim_steps;         // number of trimming steps to take
    int trim_count;         // current trim step index
    uint8_t trim_succeeded; // indicator for if the last trim step succeeded
    int trim_success_count; // counter of the number of trimming step successes

    // Fuzzing settings
    gurthang_strategy_t strat; // the current fuzzing strategy
    uint32_t last_fuzz_count; // latest retval from afl_custom_fuzz_count

    #if defined(GURTHANG_MUT_HAVOC_SUPPORT)
    uint8_t havoc_probability;  // the probability AFL++ will invoke OUR havoc
    #endif
    #if defined(GURTHANG_MUT_MEMCHECK)
    uint64_t fuzz_count;        // counter for number of fuzzing runs done
    unsigned long total_leaked; // used to save the previous leak check's count
    #endif
} gurthang_mut_t;

// Simple function to randomly select a mutation strategy. Takes in a 'free
// strategies' array that denotes which strategies have been marked for use.
static gurthang_strategy_t gurthang_strategy_choose(comux_header_t* header, int* free_strats)
{
    // if we were given a NULL header, we'll assume something went wrong with
    // parsing the comux header
    if (!header) { return STRAT_FIXUP; }

    // now, generate a random index and walk until we find one that's valid.
    // If we make a full circle and don't find a valid one, we're out of
    // options (return STRAT_UNKNOWN)
    int idx = RAND_UNDER(STRAT_LENGTH);
    int count = 0;
    while (free_strats[idx] && count++ < STRAT_LENGTH)
    { idx = (idx + 1) % STRAT_LENGTH; }
    return free_strats[idx] ? STRAT_UNKNOWN : (gurthang_strategy_t) idx;
}

// Simple function used by logging to get a string representation of a fuzzing
// strategy enum value.
static char* gurthang_strategy_string(gurthang_strategy_t strat)
{
    switch (strat)
    {
        case STRAT_CHUNK_DATA_HAVOC:
            return "CHUNK_DATA_HAVOC";
        case STRAT_CHUNK_DATA_EXTRA:
            return "CHUNK_DATA_EXTRA";
        case STRAT_CHUNK_SCHED_BUMP:
            return "CHUNK_SCHED_BUMP";
        case STRAT_CHUNK_SPLIT:
            return "CHUNK_SPLIT";
        case STRAT_CHUNK_SPLICE:
            return "CHUNK_SPLICE";
        case STRAT_CHUNK_DICT_SWAP:
            return "CHUNK_DICT_SWAP";
        default:
            return "UNKNOWN";
    }
}


// =========================== Helper Functions ============================ //
// Used to separate out any code that handles environment variable parsing and
// initialization. Called by afl_custom_init().
static void PFX(init_environment_variables)()
{
    // if the DEBUG environment variable is set, make sure logging is also
    // enabled (logging must be turned on for additional debug prints)
    char* env_debug = getenv(GURTHANG_ENV_MUT_DEBUG);
    if (env_debug)
    {
        debug_log = 1;
        if (!getenv(GURTHANG_ENV_MUT_LOG))
        {
            fatality("Please enable logging via " GURTHANG_ENV_MUT_LOG
                     " before toggling " GURTHANG_ENV_MUT_DEBUG ".");
        }
    }
    
    // check for the fuzz-min variable. This controls the minimum number of
    // fuzzing attempts made for one test case
    char* env_fmin = getenv(GURTHANG_ENV_MUT_FUZZ_MIN);
    if (env_fmin)
    {
        log_write(&mlog, "found %s=%s.", GURTHANG_ENV_MUT_FUZZ_MIN, env_fmin);

        // attempt to convert ao an integer
        long conversion = 0;
        if (str_to_int(env_fmin, &conversion) || conversion <= 0)
        { fatality("%s must be a positive integer.", env_fmin); }

        fuzz_min = (uint32_t) conversion;
        log_write(&mlog, STAB_TREE1 "minimum test case fuzz count set to %u.",
                  fuzz_min);
    }

    // check for the fuzz-max variable. This controls the maximum number of
    // fuzzing attempts made for one test case
    char* env_fmax = getenv(GURTHANG_ENV_MUT_FUZZ_MAX);
    if (env_fmax)
    {
        log_write(&mlog, "found %s=%s.", GURTHANG_ENV_MUT_FUZZ_MAX, env_fmax);
        
        // attempt to convert ao an integer
        long conversion = 0;
        if (str_to_int(env_fmax, &conversion) || conversion <= 0)
        { fatality("%s must be a positive integer.", env_fmax); }

        fuzz_max = (uint32_t) conversion;
        log_write(&mlog, STAB_TREE1 "maximum test case fuzz count set to %u.",
                  fuzz_max);

    }

    // check for the max-trim variable. This controls the maximum number of trim
    // steps for a single trimming stage
    char* env_tmax = getenv(GURTHANG_ENV_MUT_TRIM_MAX);
    if (env_tmax)
    {
        // write a message stating we found it
        log_write(&mlog, "found %s=%s.", GURTHANG_ENV_MUT_TRIM_MAX, env_tmax);
        
        // attempt to convert the integer
        long conversion = 0;
        if (str_to_int(env_tmax, &conversion))
        { fatality("%s must be an integer.", env_tmax); }

        // save to the global and adjust if it's less than zero
        trim_steps_max = conversion < 0 ? -1 : conversion;
        log_write(&mlog, STAB_TREE1 "maximum trim step count set to %ld%s.",
                  trim_steps_max, trim_steps_max < 0 ? " (no limit)" : "");
    }

    // check for the dictionary file variable
    char* env_dict = getenv(GURTHANG_ENV_MUT_DICT);
    if (env_dict)
    {
        log_write(&mlog, "found %s. Attempting to load dictionaries.",
                  GURTHANG_ENV_MUT_DICT, env_dict);
        
        // split the string by commas to retrieve file names
        char* token = NULL;
        char* str = env_dict;
        size_t dict_count = 0;
        while ((token = strtok(str, ",")))
        {
            // attempt to load a dictionary from the file path. If it failed,
            // or the dictionary only has one entry, complain and force the
            // program to exit
            dict_t* dict = dict_from_file(token);
            if (!dict || dict->size == 1)
            {
                fatality("The given dictionary file (%s) couldn't be loaded properly.\n"
                         "Please double-check the following:\n"
                         STAB_TREE2 "The file path is correct\n"
                         STAB_TREE2 "There is more than one word in the dictionary\n"
                         STAB_TREE2 "There are no duplicated words in the dictionary\n"
                         STAB_TREE1 "No empty lines are present in the file\n",
                         token);
            }
            use_dicts = 1;
            
            // if this is the first dictionary, initialize the dictionary list.
            // Then add the dictionary to the list
            if (dict_count++ == 0)
            { dllist_init(&dlist); }
            else if (dict_count > max_dicts)
            { fatality("You cannot specify more than %lu dictionaries.", max_dicts); }
            dllist_push_tail(&dlist, &dict->elem, dict);

            // write a message to the log
            log_write(&mlog, STAB_TREE2 "loaded dictionary with %lu words: %s",
                      dict->size, token);
            str = NULL;
        }
        log_write(&mlog, STAB_TREE1 "successfully loaded %lu dictionaries.", dict_count);
    }
}

// Takes in a parsed header and performs a series of checks. Returns an error
// message on failure and NULL on success.
static char* PFX(check_comux_header)(comux_header_t* header)
{
    // check maximums - complain if out of bounds
    if (header->num_conns > MAX_CONNECTIONS)
    { return "too many connections specified"; }
    if (header->num_chunks > MAX_CHUNKS)
    { return "too many chunks specified"; }

    // if we have ZERO connections or ZERO chunks, there's something wrong
    if (header->num_conns == 0)
    { return "zero connections are specified by the comux header"; }
    if (header->num_chunks == 0)
    { return "zero chunks are specified by the comux header"; }

    return NULL;
}

// Takes in a parsed cinfo and the corresponding parsed header struct for the
// comux file and performs a series of checks. Returns an error message on
// failure and NULL on success.
static char* PFX(check_comux_cinfo)(comux_header_t* header, comux_cinfo_t* cinfo)
{
    if (cinfo->id >= header->num_conns)
    { return "out-of-bounds connection ID"; }

    // make a bitmask to AND with the flags field to see if there are any
    // unsupported flags specified
    uint32_t mask = ~((uint32_t) COMUX_CHUNK_FLAGS_ALL);
    if (cinfo->flags & mask)
    { return "unsupported flag bits are enabled"; }

    return NULL;
}

// Helper function invoked by 'afl_custom_fuzz' when a badly-formatted comux
// file is read that cannot be fixed. Uses bytes from the original input buffer
// to create an entirely new comux file, writing it to a buffer and setting
// 'outbuff' accordingly.
static size_t PFX(make_new_comux)(gurthang_mut_t* mut, char* buff, size_t buff_len,
                                  char** outbuff, size_t max_len)
{
    dlog_write(&mlog, STAB_TREE1 "%shandling bad comux file.%s",
               LOG_NOT_USING_FILE(&mlog) ? C_BAD : "",
               LOG_NOT_USING_FILE(&mlog) ? C_NONE : "");

    // TODO: this would only be needed if someone decides to run this mutator
    // and preload library with with AFL++'s built-in mutations enabled (in
    // other words, with AFL_CUSTOM_MUTATOR_ONLY *not* set).
    // With the built-in mutations not knowing about the layout of comux files,
    // it will clobber important comux header and/or chunk information.
    // The clobbering would then (in some cases) be detectable by this mutator
    // and it would invoke this function to try to create a new comux file
    // from scratch.
    // I haven't implemented this yet, and chances are I won't. Plus, there's
    // not much of a reason to run AFL++ and this mutator without setting
    // AFL_CUSTOM_MUTATOR_ONLY. So I would just do that.

    *outbuff = buff;
    return buff_len;
}

// Takes in an array of cinfo structs and a particular index of interest and
// determines the range in which the cinfo's 'sched' field could change without
// affecting the relative ordering of the cinfo's connection.
// Returns 0 if the found range has at least one integer of available wiggle
// room, and non-zero otherwise. On success, 'range' has two 32-bit values
// written to it to indicate:
//      [sched_range_low_inclusive, sched_range_high_exclusive]
static uint8_t PFX(find_cinfo_sched_bounds)(comux_cinfo_t* cinfos, uint32_t cinfos_len,
                                            uint32_t index, uint32_t* range)
{
    int64_t lims[2] = {0, 0};
    // first, iterate through all cinfos and compute the next-lowets and next-
    // -highest sched fields with the given index
    uint32_t max_sched = 0;
    for (uint32_t i = 0; i < cinfos_len; i++)
    {
        // update the maximum sched value
        if (cinfos[i].sched > max_sched)
        { max_sched = cinfos[i].sched; }
        
        // skip over ourselves
        if (i == index)
        { continue; }

        // if the current chunk is assigned the same connection as the chunk we
        // randomly selected, update the min and max accordingly
        if (cinfos[i].id == cinfos[index].id)
        {
            int64_t diff = (int64_t) cinfos[i].sched - (int64_t) cinfos[index].sched;
             // if the current sched is less and one of the following is true,
             // we'll update our lower range limit:
             //  1. the current range minimum is zero
             //  2. the current range minimum is set, but this one is smaller
            if (diff < 0 && (!lims[0] || diff > lims[0]))
            { lims[0] = diff; }

             // if the current sched is greater and one of the following is
             // true, we'll update our upper range limit:
             //  1. the current range maximum is zero
             //  2. the current range maximum is set, but this one is smaller
            if (diff > 0 && (!lims[1] || diff < lims[1]))
            { lims[1] = diff; }
        }
    }
    max_sched++;

    // if the lower limit wasn't set, that means this chunk is the first one
    // for its connection. So we can set range[0] to zero, the lowest scheduling
    // value. (Otherwise, just add 'sched' back (+!) to turn it into a value
    // range of acceptable new scheds.)
    if (!lims[0])
    { lims[0] = 0; }
    else
    { lims[0] += cinfos[index].sched + 1; }

    // if the upper limit wasn't set, that means this chunk is the last one for
    // its connection. So we can adjust range[1] accordingly. (Otherwise, just
    // add 'sched' back to turn it into a value range)
    if (!lims[1])
    { lims[1] = (int64_t) max_sched; }
    else
    { lims[1] += cinfos[index].sched; }

    // if there's not enough wiggle, room, we can't compute a new scheduling value
    if (lims[1] - lims[0] < 2)
    { return -1; }

    // otherwise, write out the range values and return
    range[0] = (uint32_t) lims[0];
    range[1] = (uint32_t) lims[1];
    return 0;
}


// ========================== Mutation Strategies ========================== //
// Helper function called by 'afl_custom_fuzz' with a chunk info struct whose
// data has been parsed and saved into memory. This function is responsible for
// performing random mutations on JUST the cinfo's data segment.
static void PFX(mutate_cinfo_data_havoc)(comux_cinfo_t* cinfo)
{
    // if the chunk has NOTHING in it, don't bother
    if (cinfo->len == 0)
    { return; }

    surgical_havoc_mutate((u8*) buffer_dptr(&cinfo->data), 0,
                          buffer_size(&cinfo->data));
}

// Helper functio called by 'afl_custom_fuzz' with a cinfo struct to be
// mutated. This implements some "extra" havoc-like mutations not implemented
// by AFL++.
static void PFX(mutate_cinfo_data_extra)(comux_cinfo_t* cinfo)
{
    // if the chunk has NOTHING in it, don't bother
    if (cinfo->len == 0)
    { return; }

    char* data = buffer_dptr(&cinfo->data);
    size_t data_len = buffer_size(&cinfo->data);

    // we'll generate a random number and use it to decide which havoc-like
    // mutation we want to try
    const uint32_t num_extras = 2;
    switch (RAND_UNDER(num_extras))
    {
        case 0: // EXTRA 1: reverse bytes
            if (cinfo->len > 2)
            {
                // choose a random length of bytes to reverse, then choose a
                // random index at which to select bytes
                size_t reverse_size = RAND_UNDER(cinfo->len);
                ssize_t reverse_idx = RAND_UNDER(cinfo->len - reverse_size);
                char tmp[reverse_size];

                // copy the bytes, in reverse order, into the temporary buffer
                size_t tmp_idx = 0;
                for (ssize_t i = reverse_idx + reverse_size - 1;
                     i >= reverse_idx; i--)
                { tmp[tmp_idx++] = data[i]; }

                // write the reversed bytes into the correct spots in the
                // original chunk buffer
                for (ssize_t i = 0; i < reverse_size; i++)
                { data[i + reverse_idx] = tmp[i]; }

                dlog_write(&mlog, STAB_TREE3 STAB_TREE1 "reversed bytes %lu-%lu.",
                           reverse_idx, reverse_idx + reverse_size - 1);
                break;
            }
            // if the length isn't deemed big enough for this mutation, go to
            // the next one
        case 1: // EXTRA 2: swap two bytes' position
            if (cinfo->len > 1)
            {
                // choose two random bytes indexes in the buffer
                size_t idx1 = RAND_UNDER(data_len);
                size_t idx2 = idx1;
                while (idx1 == idx2)
                { idx2 = RAND_UNDER(data_len); }
                // swap the bytes
                char tmp = data[idx1];
                data[idx1] = data[idx2];
                data[idx2] = tmp;
                dlog_write(&mlog, STAB_TREE3 STAB_TREE1 "swapped bytes %lu and %lu.",
                           idx1, idx2);
                break;
            }
            // if the data length is less than 2 bytes, we can't do a byte
            // swap. So we'll carry onto the next mutation instead
        default:
            // we should never reach here. Keyword "should". To be safe, we'll
            // invoke surgical_havoc_mutate() here so SOMETHING is done if this
            // branch is ever hit.
            surgical_havoc_mutate((u8*) buffer_dptr(&cinfo->data), 0,
                                  buffer_size(&cinfo->data));
            break;
    }

}

// Helper function for the schedule-bumping mutation that selects a chunk
// and computes an appropriate scheduling value to change it to. It's chosen
// such that the relative order within the chunk's connection is maintained.
// If an appropriate chunk is found, its index is returned. Otherwise, -1 is
// returned to indicate a suitable chunk wasn't found.
static int64_t PFX(mutate_cinfo_sched_bump)(comux_header_t* header,
                                            comux_cinfo_t* cinfos, uint32_t cinfos_len)
{
    uint32_t lims[2] = {0, 0};
    uint32_t index = RAND_UNDER(cinfos_len);
    uint32_t count = 0;
    // Some chunks may be scheduled so tightly with its neighboring
    // same-connection chunks that we simply can't rearrange it as a
    // mutation. So, this loop will try every chunk in the array if the first
    // one isn't valid, starting from the random chunk selected above.
    while (count < cinfos_len)
    {
        // try to find a suitable range in which we can mutate this chunk's
        // scheduling value. If we found one, break out of the loop
        if (!PFX(find_cinfo_sched_bounds)(cinfos, cinfos_len, index, lims))
        { break; }

        // failure! Log it
        dlog_write(&mlog, STAB_TREE3 STAB_TREE2
                   "chunk %u isn't suitable for scheduling bumps.",
                   index);

        // increment the index (circular) and the counter
        index = (index + 1) % cinfos_len;
        count++;
    }

    // if we iterated through all of them, to no avail, return -1 to indicate
    // a failure to find a chunk we could mutate to produce different
    // scheduling behavior
    if (count == cinfos_len)
    { return -1; }
    
    // otherwise, we found a chunk whose 'sched' we can mutate. Generate random
    // integers until we get a new scheduling value different from the original
    uint32_t new_sched = cinfos[index].sched;
    while (new_sched == cinfos[index].sched)
    { new_sched = (rand() % (lims[1] - lims[0])) + lims[0]; }

    // log a few things...
    dlog_write(&mlog, STAB_TREE3 STAB_TREE2
               "bumping chunk %u's scheduling within range [%ld, %ld).",
               index, lims[0], lims[1]);
    dlog_write(&mlog, STAB_TREE3 STAB_TREE1
               "scheduling bumped from %u --> %u",
               cinfos[index].sched, new_sched);
    
    // set the cinfo's sched field and return
    cinfos[index].sched = new_sched;
    return index;
}

// Helper function for the chunk-splitting mutation. Tries to select a random
// chunk with a data segment of more than one byte and enough room within its
// same-connection scheduling, then splits it into two chunks, maintaining the
// same order within the chunk's connection.
// On success, the index at which the *NEW* chunk will be inserted is returned,
// and the original chunk's data segment will be split between itself and the
// chunk pointed at by 'new_cinfo'.
// On failure, -1 is returned.
static int64_t PFX(mutate_cinfo_split)(comux_header_t* header,
                                       comux_cinfo_t* cinfos, uint32_t cinfos_len,
                                       comux_cinfo_t* new_cinfo)
{
    uint32_t lims[2] = {0, 0};
    uint32_t index = RAND_UNDER(cinfos_len);
    uint32_t count = 0;

    // Some chunks may not be suitable for this mutation:
    //  - A chunk might not have enough data to split up (literally one byte)
    //  - A chunk might be scheduled so tightly with its neighboring chunks
    //    that we don't have enough room to make a new cinfo with a unique
    //    scheduling value.
    // So, this loop will iterate until we try all chunks.
    while (count < cinfos_len)
    {
        // try to find a suitable range in which we can mutate this chunk's
        // scheduling value. If we found one, AND the chunk has enough bytes
        // in its data segment to split it, break out of the loop
        if (!PFX(find_cinfo_sched_bounds)(cinfos, cinfos_len, index, lims) &&
            cinfos[index].len > 1)
        { break; }

        // failure! Log it
        dlog_write(&mlog, STAB_TREE3
                   "%schunk %u isn't suitable for splitting.",
                   count == cinfos_len - 1 ? STAB_TREE1 : STAB_TREE2,
                   index);

        // increment the index (circular) and the counter
        index = (index + 1) % cinfos_len;
        count++;
    }

    // if we couldn't find anything, return -1
    if (count == cinfos_len)
    { return -1; }
    
    // next, we'll take the select chunk and split its data into two
    uint64_t split_index = RAND_UNDER(cinfos[index].len - 1) + 1;
    uint64_t datalens[2] = {split_index, cinfos[index].len - split_index};
    char split_left[datalens[0] + 1];
    char split_right[datalens[1] + 1];
    snprintf(split_left, datalens[0] + 1, "%s", buffer_dptr(&cinfos[index].data));
    snprintf(split_right, datalens[1] + 1, "%s", buffer_dptr(&cinfos[index].data) + datalens[0]);

    dlog_write(&mlog, STAB_TREE3 STAB_TREE2
               "splitting chunk %u (data_len=%lu) (split_data_lens=[%lu, %lu]).",
               index, cinfos[index].len, datalens[0], datalens[1]);

    // copy over the right-side split of the bytes into the new cinfo's data
    comux_cinfo_init(new_cinfo);
    comux_cinfo_data_appendn(new_cinfo, split_right, datalens[1]);
    // reset the old cinfo's buffer and re-write the left-side split into it
    buffer_reset(&cinfos[index].data);
    cinfos[index].len = 0;
    comux_cinfo_data_appendn(&cinfos[index], split_left, datalens[0]);

    // next we need to find two scheduling values for the old and new cinfos
    // such that they maintain the ordering between themselves AND the other
    // chunks assigned to the same connection
    new_cinfo->sched = cinfos[index].sched + 1;
    while (new_cinfo->sched >= lims[1])
    {
        cinfos[index].sched--;
        new_cinfo->sched--;
    }

    dlog_write(&mlog, STAB_TREE3 STAB_TREE2
               "setting split-chunks scheduling values within range [%u, %u).",
               lims[0], lims[1]);
    dlog_write(&mlog, STAB_TREE3 STAB_TREE1
               "new scheduling values: [%u, %u]", cinfos[index].sched, new_cinfo->sched);

    // adjust a few other fields
    new_cinfo->id = cinfos[index].id;
    if (cinfos[index].flags & COMUX_CHUNK_FLAGS_AWAIT_RESPONSE)
    {
        // if the original chunk had the 'AWAIT_RESPONSE' flag set, we want
        // this to be placed on the new cinfo, since it will FOLLOW the old
        // chunk. So we'll remove it from the original and add it to the new
        cinfos[index].flags ^= COMUX_CHUNK_FLAGS_AWAIT_RESPONSE;
        new_cinfo->flags |= COMUX_CHUNK_FLAGS_AWAIT_RESPONSE;
    }
     
    return index + 1;
}

// Helper function for the chunk-splicing mutation.
// This attempts to find two chunks belonging to the same connection that are
// scheduled next to each other relative to the connection's ordering. If
// suitable chunks are found, they're combined into one, and the index of the
// chunk to REMOVE is returned.
// On failure, -1 is returned.
// On sucess, one chunk will be modified to hold both its own data and the
// spliced chunk's data, and the index of the spliced chunk (the one to remove)
// is returned.
static int64_t PFX(mutate_cinfo_splice)(comux_header_t* header,
                                        comux_cinfo_t* cinfos, uint32_t cinfos_len)
{
    // if there aren't enough chunks, return
    if (cinfos_len < 2)
    { return -1; }

    // Some connections specified within the file may not have enough chunks
    // for us to perform this mutation (we need at least two chunks belonging
    // to the same connection). So first, we'll find connection IDs that are
    // valid.
    int64_t cid_counts[header->num_conns];
    for (int64_t i = 0; i < (int64_t) header->num_conns; i++)
    { cid_counts[i] = 0; }
    for (uint32_t i = 0; i < cinfos_len; i++)
    { cid_counts[cinfos[i].id]++; }

    // with the counters we collected, pick a random connection ID and iterate
    // until we select one that had a count greater than 1, or we fail to find
    uint32_t count = 0;
    uint32_t cid = RAND_UNDER(header->num_conns);
    while (count < header->num_conns)
    {
        // if the current connection has more than 1 chunks, we'll use it
        if (cid_counts[cid] > 1)
        { break; }

        // failure! Log it
        dlog_write(&mlog, STAB_TREE3
                   "%sconnection %u doesn't have enough chunks for splicing.",
                   count == header->num_conns - 1 ? STAB_TREE1 : STAB_TREE2,
                   cid);

        // increment the index (circular) and the counter
        cid = (cid + 1) % header->num_conns;
        count++;
    }

    // if we couldn't find a connection with enough chunks, return -1
    if (count == header->num_conns)
    { return -1; }

    // now we'll build an array of indexes of chunks that belong to our
    // selected connection
    uint32_t conn_indexes[cid_counts[cid]];
    uint32_t conn_indexes_len = 0;
    for (uint32_t i = 0; i < cinfos_len; i++)
    {
        if (cinfos[i].id == cid)
        { conn_indexes[conn_indexes_len++] = i; }
    }

    // choose one of these indexes (at random, excluding the last one) to
    // select the first chunk for our splice mutation. Then select the next
    // one to form our splice pair
    uint32_t pair_index = RAND_UNDER(conn_indexes_len - 1);
    uint32_t pair[2] = {conn_indexes[pair_index], conn_indexes[pair_index + 1]};

    dlog_write(&mlog, STAB_TREE3 STAB_TREE2
               "selected chunks %u and %u (conn_id=%u) for splicing.",
               pair[0], pair[1], cid);
    dlog_write(&mlog, STAB_TREE3 STAB_TREE1
               "splicing (data_lens=[%lu, %lu]) into one chunk (data_len=%lu).",
               cinfos[pair[0]].len, cinfos[pair[1]].len,
               cinfos[pair[0]].len + cinfos[pair[1]].len);
    
    // at this point, we have our two chunks that belong to the same connection
    // and are adjacent to each other (excluding chunks from other
    // connections). Now, we'll take pair[1]'s data and append it onto
    // pair[0]'s data
    comux_cinfo_data_appendn(&cinfos[pair[0]], buffer_dptr(&cinfos[pair[1]].data),
                             buffer_size(&cinfos[pair[1]].data));
    // if pair[1]'s flags have the AWAIT_RESPONSE flag enabled, we want to copy
    // that over to pair[0], since we just took pair[1]'s data and appended it
    // to pair[0]'s.
    if (cinfos[pair[1]].flags & COMUX_CHUNK_FLAGS_AWAIT_RESPONSE)
    { cinfos[pair[0]].flags |= COMUX_CHUNK_FLAGS_AWAIT_RESPONSE; }
    
    // return the index of pair[1], the chunk we now want to delete
    return pair[1];
}

// Helper function that implements the STRAT_CHUNK_DICT_SWAP strategy. Attempts
// to find the occurrence of any word in any loaded-in dictionary. If one is
// found, it's swapped for a different word in the same dictionary, and 0 is
// returned to indicate success. If one can't be found, a non-zero value is
// returned instead.
static uint8_t PFX(mutate_cinfo_dict_swap)(comux_cinfo_t* cinfos, uint32_t cinfos_len)
{
    uint32_t index = RAND_UNDER(cinfos_len);
    uint32_t count = 0;

    // Because some chunks may not actually contain any dictionary entries,
    // we'll try all chunks in this array, starting at a random position.
    while (count < cinfos_len)
    {
        comux_cinfo_t* cinfo = &cinfos[index];
        char* data = buffer_dptr(&cinfo->data);
        size_t data_len = buffer_size(&cinfo->data);

        // iterate through each loaded-in dictionary
        dllist_elem_t* elem;
        dllist_iterate(&dlist, elem)
        {
            // get the current dictionary and pick out a random starting index
            dict_t* dict = (dict_t*) elem->container;
            size_t idx = RAND_UNDER(dict->size);
            size_t cnt = 0;

            // now, iterate through the dictionary, searching the chunk's data
            // for each entry, until we find one (or not)
            dict_entry_t* dentry = NULL;
            ssize_t dentry_offset = -1;
            while (cnt < dict->size)
            {
                // search the chunk's data for the current dictionary entry. If
                // it's found, save the offset and the entry and break
                dict_entry_t* de = &dict->entries[idx];
                char* ptr = strstr(data, de->str);
                if (ptr)
                {
                    dentry = de;
                    dentry_offset = (ssize_t) (ptr - data);
                    break;
                }

                // otherwise, increment counters to try the next one
                idx = (idx + 1) % dict->size;
                cnt++;
            }

            // if we couldn't find anything in this dictionary, try the next
            if (dentry == NULL)
            { continue; }

            // otherwise, we found one, so we want to swap it out for another.
            // Start by generating a random one from the dictionary
            dict_entry_t* swap = dentry;
            while (swap == dentry)
            { swap = dict_get_rand(dict); }

            // make a copy of all the bytes *after* the original keyword
            size_t copy_len = data_len - (dentry_offset + dentry->len);
            char copy[copy_len];
            if (copy_len > 0)
            { memcpy(copy, data + dentry_offset + dentry->len, copy_len); }

            // now, reset the chunk buffer's size manually back to where we
            // want it, and write in the key dictionary entry word plus the
            // bytes that occurrred after it
            cinfo->data.size = dentry_offset;
            buffer_appendn(&cinfo->data, swap->str, swap->len);
            buffer_appendn(&cinfo->data, copy, copy_len);
            
            // success - log and return
            dlog_write(&mlog, STAB_TREE3 STAB_TREE1
                       "swapped dictionary keyword '%s' for '%s'",
                       dentry->str, swap->str);
            return 0;
        }

        // increment counters
        index = (index + 1) % cinfos_len;
        count++;
    }

    // if we reached here, we had no luck - return
    return 1;
}

// Helper function invoked by 'afl_custom_fuzz' that takes in an array of
// parsed comux chunks and performs fuzzing on them by selecting a random
// fuzzing strategy.
// The last few parameters are used to specify if a cinfo should be added
// to the output file, or if a cinfo should be removed:
//  - new_cinfo             a pointer to a cinfo object that a mutation can
//                          fill with data if it chooses to add a new cinfo to
//                          the output file.
//  - new_cinfo_index       pointer to a integer that should be set to the
//                          index in the cinfo array at which the new cinfo
//                          should be placed. (set to -1 by default)
//  - delete_cinfo_index    pointer to an integer used to specify an index in
//                          the cinfo array to delete a cinfo.
// The mutator shouldn't specify BOTH a 'new_cinfo_index' and a
// 'delete_cinfo_index'.
static void PFX(mutate_cinfos)(gurthang_mut_t* mut, comux_header_t* header,
                               comux_cinfo_t* cinfos, uint32_t cinfos_len,
                               comux_cinfo_t* new_cinfo, int64_t* new_cinfo_index,
                               int64_t* delete_cinfo_index)
{
    // -------------------- FUZZING STRATEGY SELECTION --------------------- //
    // to keep track of which strategies we have and haven't tried for this
    // input, we'll use an integer value to represent the state of each strat.
    // If it's zero, it's good for use. If not, it can't be chosen. If all are
    // zero, we give up on this input.
    int free_strats[STRAT_LENGTH];
    memset(free_strats, 0, sizeof(int) * STRAT_LENGTH);
    
    // if the header doesn't have more than one connection, we can't select
    // the SCHED_BUMP mutation strategy
    if (header->num_conns < 2)
    { free_strats[STRAT_CHUNK_SCHED_BUMP]++; }

    // if we don't have any dictionaries, we can't use STRAT_CHUNK_DICT_SWAP
    if (!use_dicts)
    { free_strats[STRAT_CHUNK_DICT_SWAP]++; }

    // now, choose the strategy. If the mutator's strat field has already been
    // set, we'll use that. If not, we'll generate one from our list of free
    // strategies
    gurthang_strategy_t strat = mut->strat == STRAT_UNKNOWN ?
                                 gurthang_strategy_choose(header, free_strats) :
                                 mut->strat;
    dlog_write(&mlog, STAB_TREE2 "chosen strategy: %s.%s",
               gurthang_strategy_string(strat),
               mut->strat != STRAT_UNKNOWN ? " (override)" : "");

    // RETRY LABEL: used if a fuzzing strategy, for some reason, doesn't work
    // on the given input. The caller of 'goto retry_strategy' should first
    // set the strategy to something OTHER than what it previously was.
    retry_strategy:
    // if we enter this section with the strategy set to UNKNOWN, then we're
    // completely out of valid strategies to try. Reset the strategy field and
    // return
    if (strat == STRAT_UNKNOWN)
    {
        mut->strat = STRAT_UNKNOWN;
        dlog_write(&mlog, STAB_TREE1, "no valid strategies found.");
        return;
    }
    // -------------------------- ACTUAL FUZZING --------------------------- //
    // we've picked out a fuzzing strategy and selected one or two chunks to
    // mutate. Now we'll actually carry out the fuzzing.
    switch (strat)
    {
        case STRAT_CHUNK_DATA_HAVOC:
            PFX(mutate_cinfo_data_havoc)(&cinfos[RAND_UNDER(header->num_chunks)]);
            buffer_appendf(&mut->dbuff, "chunk_havoc");
            break;
        case STRAT_CHUNK_DATA_EXTRA:
            PFX(mutate_cinfo_data_extra)(&cinfos[RAND_UNDER(header->num_chunks)]);
            buffer_appendf(&mut->dbuff, "chunk_extra");
            break;
        case STRAT_CHUNK_SCHED_BUMP:
            // *try* to modify a chunk's scheduling value. Try something else
            // on failure
            if (PFX(mutate_cinfo_sched_bump)(header, cinfos, cinfos_len) == -1)
            {
                free_strats[STRAT_CHUNK_SCHED_BUMP]++;
                strat = gurthang_strategy_choose(header, free_strats);
                dlog_write(&mlog, STAB_TREE2 "failed to find a suitable chunk. Switching to %s",
                           gurthang_strategy_string(strat));
                goto retry_strategy;
            }
            buffer_appendf(&mut->dbuff, "chunk_sched_bump");
            break;
        case STRAT_CHUNK_SPLIT:
            // try to find a chunk and split it. Try something else on failure
            *new_cinfo_index = PFX(mutate_cinfo_split)(header, cinfos, cinfos_len, new_cinfo);
            if (*new_cinfo_index == -1)
            {
                free_strats[STRAT_CHUNK_SPLIT]++;
                strat = gurthang_strategy_choose(header, free_strats);
                dlog_write(&mlog, STAB_TREE2 "failed to find a suitable chunk. Switching to %s",
                           gurthang_strategy_string(strat));
                goto retry_strategy;
            }
            buffer_append(&mut->dbuff, "chunk_split");
            break;
        case STRAT_CHUNK_SPLICE:
            // try to find a chunk to splice, and splice it. Try something else
            // on failure
            *delete_cinfo_index = PFX(mutate_cinfo_splice)(header, cinfos, cinfos_len);
            if (*delete_cinfo_index == -1)
            {
                free_strats[STRAT_CHUNK_SPLICE]++;
                strat = gurthang_strategy_choose(header, free_strats);
                dlog_write(&mlog, STAB_TREE2 "failed to find suitable chunks. Switching to %s",
                           gurthang_strategy_string(strat));
                goto retry_strategy;
            }
            buffer_append(&mut->dbuff, "chunk_splice");
            break;
        case STRAT_CHUNK_DICT_SWAP:
            // attempt to mutate a single chunk - on failure, try another strat
            if (PFX(mutate_cinfo_dict_swap)(cinfos, cinfos_len))
            {
                free_strats[STRAT_CHUNK_DICT_SWAP]++;
                strat = gurthang_strategy_choose(header, free_strats);
                dlog_write(&mlog, STAB_TREE2 "failed to find any dictionary entries. "
                           "Switching to %s", gurthang_strategy_string(strat));
                goto retry_strategy;
            }
            buffer_append(&mut->dbuff, "chunk_dict_swap");
            break;
        default:
            // if, for some reason, we have a case not specified above, we'll
            // just perform a havoc mutation on a chunk's data
            PFX(mutate_cinfo_data_havoc)(&cinfos[RAND_UNDER(header->num_chunks)]);
            break;
    }
    
    // reset the mutator's 'strat' field for the next fuzz
    mut->strat = STRAT_UNKNOWN;
}


// ========================== AFL++ Mutator Hooks ========================== //
// The mutator's initialization function.
gurthang_mut_t* afl_custom_init(afl_state_t* afl, unsigned int seed)
{
    // attempt to allocate a mutator struct to return to AFL++ and initialize
    // its inner fields
    gurthang_mut_t* mut = alloc_check(sizeof(gurthang_mut_t));
    mut->afl = afl;
    buffer_init(&mut->buff, 1 << 20);
    buffer_init(&mut->dbuff, 1 << 9);

    // set up trimming variables
    buffer_init(&mut->tbuff_head, 1 << 19);
    buffer_init(&mut->tbuff_tail, 1 << 19);
    buffer_init(&mut->tbuff, 1 << 20);
    comux_cinfo_init(&mut->trim_cinfo);
    comux_cinfo_init(&mut->trim_cinfo_old);
    mut->trim_steps = 0;
    mut->trim_count = 0;
    mut->trim_succeeded = 1;
    mut->trim_success_count = 0;

    // set up initial fuzzing options
    mut->strat = STRAT_UNKNOWN;
    mut->last_fuzz_count = 0;

    #if defined(GURTHANG_MUT_HAVOC_SUPPORT)
    mut->havoc_probability = 100;
    #endif
    #if defined(GURTHANG_MUT_MEMCHECK)
    mut->fuzz_count = 0;
    mut->total_leaked = 0;
    #endif

    // seed the random generator, initialize the log, and read any environment
    // variables the user might have supplied.
    srand(seed);
    log_init(&mlog, "gurthang-mut", GURTHANG_ENV_MUT_LOG);
    PFX(init_environment_variables)();

    // log and return
    log_write(&mlog, "mutator initialized.");
    return mut;
}

// The mutator's de-initialization function.
void afl_custom_deinit(gurthang_mut_t* mut)
{
    // log one last message, then destroy the log
    log_write(&mlog, "mutator de-initialized.");
    log_free(&mlog);

    // free buffers
    buffer_free(&mut->buff);
    buffer_free(&mut->dbuff);
    buffer_free(&mut->tbuff_head);
    buffer_free(&mut->tbuff_tail);
    buffer_free(&mut->tbuff);

    // free any dictionaries
    while (dlist.size > 0)
    {
        dllist_elem_t* e = dllist_pop_head(&dlist);
        dict_free((dict_t*) e->container);
        free(e->container);
    }
}

// Main mutating function. Takes in the following parameters:
//  - mut           reference to the mutator struct returned from init()
//  - buff          the input buffer
//  - buff_len      the length of the input buffer
//  - outbuff       the output buffer to write to
//  - addbuff       an additional buffer to add a test case to
//  - addbuff_len    the length of the additional buffer
//  - max_len       the maximum length of outbuff can hold
// This returns the number of bytes written to outbuff.
size_t afl_custom_fuzz(gurthang_mut_t* mut, char* buff, size_t buff_len,
                       char** outbuff, char* addbuff, size_t addbuff_len,
                       size_t max_len)
{
    #if defined(GURTHANG_MUT_MEMCHECK)
    // periodically perform memory leak checks
    if (mut->fuzz_count % 1000000 == 0)
    {
        VALGRIND_PRINTF("GURTHANG_MUT_MEMCHECK: fuzz_count=%lu\n",
                        mut->fuzz_count);
        VALGRIND_DO_LEAK_CHECK;

        // retrieve the latest count of leaked bytes
        unsigned long leaked = 0;
        unsigned long possibly = 0;
        unsigned long reachable = 0;
        unsigned long suppressed = 0;
        VALGRIND_COUNT_LEAKS(leaked, possibly, reachable, suppressed);
        mut->total_leaked = leaked;
        VALGRIND_PRINTF("GURTHANG_MUT_MEMCHECK: "
                        "[leaked=%lu, possibly=%lu, reachable=%lu, suppressed=%lu]\n",
                        leaked, possibly, reachable, suppressed);
    }
    mut->fuzz_count++;
    #endif

    flog_write(&mlog, "fuzzing test case: buff_len=%lu, max_len=%lu",
               buff_len, max_len);

    // set up variables for reading/parsing (and clear our reusable buffer)
    size_t total_rcount = 0;
    size_t rcount = 0;
    ssize_t wcount = 0;
    buffer_reset(&mut->buff);

    // ----------------------- COMUX HEADER PARSING ------------------------ //
    // first, try to parse the header from the buffer
    comux_header_t header;
    comux_header_init(&header);
    comux_parse_result_t pr = comux_header_read_buffer(&header, buff, buff_len, &rcount);
    if (pr)
    {
        dlog_write(&mlog, STAB_TREE2 "failed to read the header: %s.",
                   comux_parse_result_string(pr));
        return PFX(make_new_comux)(mut, buff, buff_len, outbuff, max_len);
    }
    total_rcount += rcount;

    // perform a series of checks on the parsed header
    char* emsg = PFX(check_comux_header)(&header);
    if (emsg)
    {
        dlog_write(&mlog, STAB_TREE2 "found an issue with the header: %s.",
                   emsg);
        return PFX(make_new_comux)(mut, buff, buff_len, outbuff, max_len);
    }

    // hard-set the version number
    header.version = 0;

    // ------------------------ COMUX CHUNK PARSING ------------------------ //
    // next, try to iterate through each chunk and read each comux chunk
    uint32_t num_chunks = header.num_chunks;
    comux_cinfo_t cinfos[num_chunks];
    for (uint32_t i = 0; i < num_chunks; i++)
    {
        comux_cinfo_t* cinfo = &cinfos[i];
        // initialize and attempt to read the cinfo header data
        comux_cinfo_init(cinfo);
        comux_parse_result_t pr;
        pr = comux_cinfo_read_buffer(cinfo,
                                     buff + total_rcount,
                                     MAX(0, (ssize_t) buff_len - (ssize_t) total_rcount),
                                     &rcount);
        // if reading of the current cinfo header failed, log and handle
        if (pr)
        {
            dlog_write(&mlog, STAB_TREE1 "failed to read chunk %u: %s.",
                       i, comux_parse_result_string(pr));
            return PFX(make_new_comux)(mut, buff, buff_len, outbuff, max_len);
        }
        total_rcount += rcount;

        // fix up the flags such that any unsupported bits are NOT enabled,
        // then perform a few more checks to ensure the chunk header looks ok
        cinfo->flags = cinfo->flags & COMUX_CHUNK_FLAGS_ALL;
        emsg = PFX(check_comux_cinfo)(&header, cinfo);
        if (emsg)
        {
            dlog_write(&mlog, STAB_TREE2 "found an issue with chunk %u: %s.",
                       i, emsg);
            return PFX(make_new_comux)(mut, buff, buff_len, outbuff, max_len);
        }

        // force-disable the NO_SHUTDOWN flag, if applicable. This will cause
        // hangs when handled by the preload library, which AFL++ would flag. We
        // don't want any false-positive hangs.
        cinfo->flags = cinfo->flags & ~COMUX_CHUNK_FLAGS_NO_SHUTDOWN;

        // read the cinfo's data into its buffer
        rcount = comux_cinfo_data_read_buffer(cinfo, buff + total_rcount,
                                              MAX(0, (ssize_t) buff_len - (ssize_t) total_rcount));
        total_rcount += rcount;

        // update the data length if the number of bytes we just read doesn't
        // match the chunk's 'len' field
        cinfo->len = cinfo->len == rcount ? cinfo->len : rcount;
    }

    // ------------------------ COMUX CHUNK FUZZING ------------------------ //
    // now that we've parsed all the headers, we'll fuzz. First, reset our
    // mutation-description buffer and append a prefix to it (this will be used
    // if a crash/hang is detected and AFL++ invokes afl_custom_describe().)
    buffer_reset(&mut->dbuff);
    buffer_appendf(&mut->dbuff, "ss_");
    // set up a few needed fields (used to adding/removing cinfos) then invoke
    // the main mutation function
    comux_cinfo_t new_cinfo;
    int64_t new_cinfo_index = -1;
    int64_t delete_cinfo_index = -1;
    PFX(mutate_cinfos)(mut, &header, cinfos, num_chunks,
                       &new_cinfo, &new_cinfo_index, &delete_cinfo_index);
    
    // depending on what was specified, we'll increase or decrease the number
    // of chunks specified by the header before we write it out
    if (new_cinfo_index > -1)
    { header.num_chunks++; }
    else if (delete_cinfo_index > -1)
    { header.num_chunks--; }
    
    // ----------------------- COMUX HEADER WRITING ------------------------ //
    // write the header out to our output buffer
    wcount = comux_header_write_buffer(&header, buffer_nptr(&mut->buff), max_len);
    if (wcount < 0)
    {
        dlog_write(&mlog, STAB_TREE1 "not enough buffer space to write the header. "
                   "No mutations done.");
        // return the exact same buffer that was given as input
        *outbuff = buff;
        return buff_len;
    }
    buffer_size_increase(&mut->buff, wcount);

    // ------------------------ COMUX CHUNK WRITING ------------------------ //
    // iterate, again, through the chunks, and write them out. We iterate one
    // extra time (one more than the number of chunks) in case we need to
    // insert a new chunk at the very end.
    for (uint32_t i = 0; i < num_chunks + 1; i++)
    {
        comux_cinfo_t* cinfo = NULL;
        // if there isn't a new chunk to insert at the end, skip the final
        // iteration. Otherwise, we'll just grab the current chunk as usual
        uint8_t new_cinfo_match = new_cinfo_index > -1 && (uint32_t) new_cinfo_index == i;
        if (i == num_chunks)
        {
            if (!new_cinfo_match)
            { continue; }
        }
        else
        { cinfo = &cinfos[i]; } // grab current chunk

        // if we were given a chunk index to "delete" from the mutation above,
        // and we just hit that index, we'll skip this iteration entirely to
        // prevent the chunk from getting written out (effectively deleting it)
        if (cinfo && delete_cinfo_index > -1 && (uint32_t) delete_cinfo_index == i)
        {
            comux_cinfo_free(cinfo);
            continue;
        }

        // if we were given an index to insert a new chunk, check for that here
        if (new_cinfo_match)
        { cinfo = &new_cinfo; }

        // write the header out to the output buffer
        wcount = comux_cinfo_write_buffer(cinfo, buffer_nptr(&mut->buff),
                                          max_len - buffer_size(&mut->buff));
        if (wcount < 0)
        {
            dlog_write(&mlog, STAB_TREE1 "not enough buffer space to write chunk "
                       "%u's header. No mutations done.", i);
            // return the exact same buffer that was given as input
            *outbuff = buff;
            return buff_len;
        }
        buffer_size_increase(&mut->buff, wcount);

        // then, attempt to write the data out to the output buffer
        wcount = comux_cinfo_data_write_buffer(cinfo, buffer_nptr(&mut->buff),
                                               max_len - buffer_size(&mut->buff));
        if (wcount < 0)
        {
            dlog_write(&mlog, STAB_TREE1 "not enough buffer space to write chunk "
                       "%u's data. No mutations done.", i);
            // return the exact same buffer that was given as input
            *outbuff = buff;
            return buff_len;
        }
        buffer_size_increase(&mut->buff, wcount);
        
        // free the chunk's memory
        comux_cinfo_free(cinfo);

        // if this iteration was the writing-out of a new cinfo (added via a
        // mutation), back the iterator up and reset the 'new_cinfo_index' so
        // we can write-out cinfos[i]
        if (new_cinfo_match)
        {
            i--;
            new_cinfo_index = -1;
        }
    }

    // we reached the end with no issues - log it
    dlog_write(&mlog, STAB_TREE1 "%sall good!%s",
               LOG_NOT_USING_FILE(&mlog) ? C_GOOD : "",
               LOG_NOT_USING_FILE(&mlog) ? C_NONE : "");
    
    // point the outbuff pointer to the correct spot, and return the correct
    // size of the fuzzed data
    *outbuff = buffer_dptr(&mut->buff);
    return buffer_size(&mut->buff);
}

#if defined(GURTHANG_MUT_HAVOC_SUPPORT)
// This is invoked by AFL++ during the havoc mutation stage and performs a
// single havoc-like mutation on the given input. It's stacked with other
// built-in mutations during the havoc stage.
// The parameters and return value are the same as in afl_custom_fuzz().
size_t afl_custom_havoc_mutation(gurthang_mut_t* mut, char* buff, size_t buff_len,
                                 char** outbuff, size_t max_len)
{
    // to make things simple, we can simply set the mutator's 'strat' field to
    // the havoc strategy, then invoke afl_custom_fuzz(). Setting mut->strat
    // will tell the fuzzing procedure to NOT choose randomly.
    mut->strat = STRAT_CHUNK_DATA_HAVOC;
    
    flog_write(&mlog, "passing test case to %safl_custom_fuzz%s: buff_len=%lu, max_len=%lu",
               LOG_NOT_USING_FILE(&mlog) ? C_FUNC : "",
               LOG_NOT_USING_FILE(&mlog) ? C_NONE : "",
               buff_len, max_len);
    return afl_custom_fuzz(mut, buff, buff_len, outbuff, NULL, 0, max_len);
}

// This simple function returns the probability with which AFL++ will invoke
// our custom mutator's havoc mutation (above). The default is 6%.
// This appears to be called by AFL++ at the start of each havoc phase.
uint8_t afl_custom_havoc_mutation_probability(gurthang_mut_t* mut)
{
    flog_write(&mlog, "probability to invoke OUR havoc mutation: %u%%",
               mut->havoc_probability);
    return mut->havoc_probability;
}

#endif

// AFL++ will hand the mutator file paths pointing to entries in its queue. We
// get to influence whether or not AFL++ should use this queue entry by:
//  - Returning '1' here if we want AFL++ to use the input file
//  - Returning '0' here if we DON'T want AFL++ to use the input file
uint8_t afl_custom_queue_get(gurthang_mut_t* mut, const char* fpath)
{
    flog_write(&mlog, "judging test case: fpath=%s", fpath);

    // attempt to open the file for reading
    int fd = open(fpath, O_RDONLY);
    if (fd == -1)
    {
        dlog_write(&mlog, "failed to open file %s for reading: %s",
                   fpath, strerror(errno));
        return 0; // failed to open - definitely skip this one
    }

    // next, try to read the comux header from the file
    comux_header_t header;
    comux_header_init(&header);
    comux_parse_result_t pr = comux_header_read(&header, fd);
    if (pr)
    {
        dlog_write(&mlog, STAB_TREE1
                   "failed to read the header: %s. Denying.",
                   comux_parse_result_string(pr));
        close(fd);
        return 0;
    }

    // check a few fields within the header
    char* emsg = PFX(check_comux_header)(&header);
    if (emsg)
    {
        dlog_write(&mlog, STAB_TREE1
                   "found an issue with the header: %s. Denying.",
                   emsg);
        close(fd);
        return 0;
    }

    // next, iterate through all specified chunks and try to parse them
    comux_cinfo_t cinfo;
    for (uint32_t i = 0; i < header.num_chunks; i++)
    {
        // initialize and attempt to read the cinfo header data
        comux_cinfo_init(&cinfo);
        comux_parse_result_t pr = comux_cinfo_read(&cinfo, fd);
        if (pr)
        {
            dlog_write(&mlog, STAB_TREE1 "failed to read chunk %u: %s. Denying.",
                       i, comux_parse_result_string(pr));
            close(fd);
            return 0;
        }

        // perform some checks on the current cinfo object
        emsg = PFX(check_comux_cinfo)(&header, &cinfo);
        if (emsg)
        {
            dlog_write(&mlog, STAB_TREE1 "found an issue with chunk %u: %s. Denying.",
                       i, emsg);
            close(fd);
            return 0;
        }

        // skip past the chunk's data segment - we're just interested in
        // verifying each chunk's header data. If we fail to seek, we'll assume
        // something is off about the cinfo's length field, and deny this file
        if (lseek(fd, cinfo.len, SEEK_CUR) == -1)
        {
            dlog_write(&mlog, STAB_TREE1 "failed to seek past chunk %u data "
                       "segment: %s. Denying.", i, strerror(errno));
            close(fd);
            return 0;
        }
    }

    // everything passed above - accept the file
    dlog_write(&mlog, STAB_TREE1 "everything looks good. Accepting.");
    close(fd);
    return 1;
}

// This function is invoked when deciding how many fuzzing attempts to perform
// on a specific input (contained within 'buff'). Typically, AFL++ decides this
// on its own based on a few factors, but by implementing this function, we can
// force its hand.
unsigned int afl_custom_fuzz_count(gurthang_mut_t* mut, char* buff, size_t buff_len)
{
    // get AFL++'s previous maximum and compute a reduced number, in the event
    // we need to reduce the number. We'll also set up 'adjusted_fuzz_count',
    // which will be increased/decreased depending on how "interesting" we
    // think comux file is.
    // NOTE: AFL++ keeps track of this internally. Accessed through:
    //      mut->afl->stage_max
    // Although, as of v4.00, it seems this value is zeroed out. What gives?
    uint32_t current_fuzz_count = MAX(fuzz_min, mut->last_fuzz_count);
    uint32_t reduced_fuzz_count = MAX(fuzz_min, current_fuzz_count / 8);
    uint32_t adjusted_fuzz_count = current_fuzz_count;
    uint32_t count_decrease_threshold = (((fuzz_max - fuzz_min) * 3) / 4) + fuzz_min;
    flog_write(&mlog, "inspecting input (previous fuzz count: %u)",
               current_fuzz_count);
    
    // set up counters for reading the buffer
    size_t total_rcount = 0;
    size_t rcount = 0;

    // attempt to parse the comux header from the buffer. We'll use this to
    // determine how interesting the test case might be
    comux_header_t header;
    comux_header_init(&header);
    comux_parse_result_t pr = comux_header_read_buffer(&header, buff, buff_len, &rcount);
    // if parsing failed, there's something wrong with the comux file, so we
    // don't want to fuzz it as much. Reduce the count
    if (pr)
    {
        dlog_write(&mlog, STAB_TREE1 "failed to parse the comux header: %s. "
                   "Reducing. (%u --> %u)", comux_parse_result_string(pr),
                   current_fuzz_count, reduced_fuzz_count);
        mut->last_fuzz_count = reduced_fuzz_count;
        return reduced_fuzz_count;
    }
    total_rcount += rcount;

    // perform a few checks of the header. If they fail, we'll reduce
    char* emsg = PFX(check_comux_header)(&header);
    if (emsg)
    {
        dlog_write(&mlog, STAB_TREE1 "found an issue with the header: %s. "
                   "Reducing. (%u --> %u)",
                   emsg, current_fuzz_count, reduced_fuzz_count);
        mut->last_fuzz_count = reduced_fuzz_count;
        return reduced_fuzz_count;
    }

    // if this comux input has multiple connections specified within it, that's
    // interesting! Multiple connections could lead to more concurrency-related
    // bugs in the target server. So, we'll increase the count
    if (header.num_conns > 1)
    {
        adjusted_fuzz_count *= MAX(3, header.num_conns);
        dlog_write(&mlog, STAB_TREE2 "multiple connections specified.");
    }
    // otherwise, if the comux input only has one connection, we'll decrease it
    // *slightly* and return. Decrease only if the current fuzz count is
    // nearing the maximum allowed amount
    else if (mut->last_fuzz_count >= count_decrease_threshold)
    {
        adjusted_fuzz_count /= 2;
        dlog_write(&mlog, STAB_TREE2 "only one connection specified.");
    }

    // next, we'll try to read each cinfo header and check for issues
    comux_cinfo_t cinfo;
    for (uint32_t i = 0; i < header.num_chunks; i++)
    {
        // initialize and attempt to read the cinfo header data
        comux_cinfo_init(&cinfo);
        comux_parse_result_t pr;
        pr = comux_cinfo_read_buffer(&cinfo,
                                     buff + total_rcount,
                                     MAX(0, (ssize_t) buff_len - (ssize_t) total_rcount),
                                     &rcount);
        
        // if reading of the current cinfo header failed, we'll reduce
        if (pr)
        {
            dlog_write(&mlog, STAB_TREE1 "failed to read chunk %u: %s. "
                       "Reducing. (%u --> %u)",
                       i, comux_parse_result_string(pr),
                       current_fuzz_count, reduced_fuzz_count);
            mut->last_fuzz_count = reduced_fuzz_count;
            return reduced_fuzz_count;
        }
        total_rcount += rcount;

        // perform a series of checks for the cinfo's header information
        emsg = PFX(check_comux_cinfo)(&header, &cinfo);
        if (emsg)
        {
            dlog_write(&mlog, STAB_TREE1 "found an issue with chunk %u: %s. "
                       "Reducing. (%u --> %u)",
                       i, emsg, current_fuzz_count, reduced_fuzz_count);
            mut->last_fuzz_count = reduced_fuzz_count;
            return reduced_fuzz_count;
        }

        // skip to the next comux chunk header, according to the current one's
        // data length field
        total_rcount += cinfo.len;
    }

    // if this comux input has lots of chunks, that's interesting! Multiple
    // chunks could mean we have messages split up across several chunks,
    // which may lead to some interesting bugs in the target server
    if (header.num_chunks > header.num_conns)
    {
        adjusted_fuzz_count *= MAX(3, header.num_chunks - header.num_conns);
        dlog_write(&mlog, STAB_TREE2 "several chunks specified.");
    }
    // otherwise, if not many chunks are specified, we'll decrease slightly
    // (only decrease if the latest fuzz count is nearing the maximum)
    else if (mut->last_fuzz_count >= count_decrease_threshold)
    {
        adjusted_fuzz_count /= 2;
        dlog_write(&mlog, STAB_TREE2 "only a few chunks specified.");
    }

    // make sure we're within the accepted min/max bounds, and save and return
    adjusted_fuzz_count = MIN(fuzz_max, adjusted_fuzz_count);
    adjusted_fuzz_count = MAX(fuzz_min, adjusted_fuzz_count);
    dlog_write(&mlog, STAB_TREE1 "adjusted fuzz count: %u --> %u",
               mut->last_fuzz_count, adjusted_fuzz_count);
    mut->last_fuzz_count = adjusted_fuzz_count;
    return adjusted_fuzz_count;
}

// This function is called by AFL++ to help describe the name of an output file
// based on what mutations this mutator performed.
char* afl_custom_describe(gurthang_mut_t* mut, size_t max_len)
{
    return buffer_dptr(&mut->dbuff);
}


// ===================== AFL++ Trimming Mutator Hooks ====================== //
// As explained in the AFL++ custom mutator documentation, a single trimming
// stage involves a single input file, but is comprised of multiple steps.
// During each step, the input file is trimmed down a little bit, then AFL++
// re-runs the trimmed version to determine if it still causes the target
// program to behave in the same manner.
// If it DOES behave the same with the trimmed-down input, it's kept and
// trimmed even further in the next trimming step. If it DOESN'T behave the
// same way, it's thrown out and the most recent "good" version is restored for
// the next trimming step.
//
// Gurthang's trimming strategy works like this:
//  1. Parse the given comux file to determine how many chunks are present.
//  2. Choose one of these chunks at random (we'll call this "C")
//  3. Choose a set number of bytes (N) to remove during each step.
//  4. For each trimming step:
//      4-1. Remove N random bytes from C's data segment.
//      4-2. If trimming succeeded, great. Use the new version for the next step.
//      4-3. If trimming failed:
//          4-3-1. Reset C back to its previous data segment.
//          4-3-2. If, after 100 trims or 25% of the total trimming steps
//                 (whichever comes first), we have less than a 10% success
//                 rate, give up on trimming.
//
// Effectively it's "keep removing N random bytes, one at a time, unless we're
// accomplishing nothing."

// Custom trim initialization for one particular test case. This takes in the
// test case's buffer and its length, and returns the number of trimming stages
// to try (AKA, the number of times to call afl_custom_trim.)
int afl_custom_init_trim(gurthang_mut_t* mut, char* buff, size_t buff_len)
{
    flog_write(&mlog, "initializing trim stage.");

    // free any memory from previous runs
    static uint64_t afl_custom_init_trim_count = 0;
    if (afl_custom_init_trim_count++ > 0)
    {
        comux_cinfo_free(&mut->trim_cinfo);
        comux_cinfo_free(&mut->trim_cinfo_old);
    }

    // reset the trimming variables
    buffer_reset(&mut->tbuff_head);
    buffer_reset(&mut->tbuff_tail);
    buffer_reset(&mut->tbuff);
    mut->trim_steps = 0;
    mut->trim_count = 0;
    comux_cinfo_init(&mut->trim_cinfo);
    comux_cinfo_init(&mut->trim_cinfo_old);
    mut->trim_cinfo_old_size = 0;
    mut->trim_bytes_per_step = 1;
    mut->trim_succeeded = 1;
    mut->trim_success_count = 0;
    
    // parse the comux file's header from the buffer
    comux_header_t header;
    comux_header_init(&header);
    size_t total_rcount = 0;
    size_t rcount = 0;
    comux_parse_result_t pr = comux_header_read_buffer(&header, buff, buff_len, &rcount);
    if (pr)
    {
        dlog_write(&mlog, STAB_TREE2 "failed to read the header: %s. "
                   "No trimming will occur.", comux_parse_result_string(pr));
        return 0;
    }
    total_rcount += rcount;

    // perform a series of checks on the parsed header
    char* emsg = PFX(check_comux_header)(&header);
    if (emsg)
    {
        dlog_write(&mlog, STAB_TREE2 "found an issue with the header: %s. "
                   "No trimming will occur.", emsg);
        return 0;
    }

    // next, we'll pick a random chunk to attempt trimming during this stage
    uint32_t cidx = RAND_UNDER(header.num_chunks);
    size_t cinfo_offset = 0;
    size_t cinfo_total_len = 0;
    dlog_write(&mlog, STAB_TREE2 "selected chunk %u for trimming.", cidx);
    
    // read the chunk headers until we reach the selected one
    for (uint32_t i = 0; i <= cidx; i++)
    {
        // if this is our desired index, save the offset
        cinfo_offset = i == cidx ? total_rcount : cinfo_offset;

        // initialize and attempt to read the cinfo header data
        comux_cinfo_init(&mut->trim_cinfo);
        comux_parse_result_t pr;
        pr = comux_cinfo_read_buffer(&mut->trim_cinfo,
                                     buff + total_rcount,
                                     MAX(0, (ssize_t) buff_len - (ssize_t) total_rcount),
                                     &rcount);
        // if reading of the current cinfo header failed, log and handle
        if (pr)
        {
            dlog_write(&mlog, STAB_TREE1 "failed to read chunk %u: %s. "
                       "No trimming will occur.",
                       i, comux_parse_result_string(pr));
            return 0;
        }
        total_rcount += rcount;
        cinfo_total_len = i == cidx ? cinfo_total_len + rcount : cinfo_total_len;

        // if this is the desired chunk, we'll also read its data segment
        if (i == cidx)
        {
            cinfo_total_len += mut->trim_cinfo.len;
            comux_cinfo_data_read_buffer(&mut->trim_cinfo, buff + total_rcount,
                                         MAX(0, (ssize_t) buff_len - (ssize_t) total_rcount));
        }
        // otherwise, we'll skip past this chunk's data segment
        else
        { total_rcount += mut->trim_cinfo.len; }
    }
    mut->trim_cinfo_old_size = cinfo_total_len;

    // determine the number of bytes to remove during each trimming step. We'll
    // scale this by the size of the chunk we've chosen (at minimum, a single
    // byte per step)
    float bps = MAX(1.0, 0.025 * (float) mut->trim_cinfo.len);
    mut->trim_bytes_per_step = (size_t) bps;

    // at this point we'll break the entire buffer into three sections (using
    // our three trimming buffers)
    //  1. bytes occurring BEFORE our selected chunk. These won't change
    //  2. the bytes of our selected chunk. These WILL change as we trim (these
    //     bytes have already been saved into mut->trim_cinfo)
    //  3. bytes occurring AFTER our selected chunk. These won't change
    buffer_appendn(&mut->tbuff_head, buff, cinfo_offset);
    buffer_appendn(&mut->tbuff_tail, buff + cinfo_offset + cinfo_total_len,
                   buff_len - (cinfo_offset + cinfo_total_len));
    
    // append the head bytes to the output buffer. We won't need to touch this
    // again in any of the trimming stages
    buffer_appendn(&mut->tbuff, buff, cinfo_offset);

    // we'll take a byte-by-byte approach for trimming. But we don't want to
    // spend TOO much time trimming, so we'll cap it off at some maximum value
    mut->trim_steps = (mut->trim_cinfo.len / mut->trim_bytes_per_step) - 1;
    if (trim_steps_max > -1 && mut->trim_steps > trim_steps_max)
    { mut->trim_steps = trim_steps_max; }
    dlog_write(&mlog, STAB_TREE1 "initialized trim stage with %d steps%s. "
               "Removing roughly %lu byte(s) per step.",
               mut->trim_steps,
               mut->trim_steps == trim_steps_max ? " (capped)" : "",
               mut->trim_bytes_per_step);
    return mut->trim_steps;
}

// Custom trim stage. Takes the buffer given in afl_custom_init_trim and trims
// it down in some way, writing it out to *outbuff and returning the size of
// the trimmed test case.
size_t afl_custom_trim(gurthang_mut_t* mut, char** outbuff)
{
    flog_write(&mlog, "trimming step %d/%d. %d steps remain.",
               mut->trim_count + 1, mut->trim_steps,
               mut->trim_steps - (mut->trim_count + 1));
    size_t old_size = buffer_size(&mut->tbuff_head) +
                      mut->trim_cinfo_old_size +
                      buffer_size(&mut->tbuff_tail);
    
    // make a copy of the current chunk state, in case this next trimming step
    // doesn't work out (and we have to reset). We only need to do this if the
    // last trim step succeeded
    if (mut->trim_succeeded)
    {
        comux_cinfo_free(&mut->trim_cinfo_old); // free anything old
        comux_cinfo_init(&mut->trim_cinfo_old); // re-initialize
        mut->trim_cinfo_old.id = mut->trim_cinfo.id;
        mut->trim_cinfo_old.len = mut->trim_cinfo.len;
        mut->trim_cinfo_old.sched = mut->trim_cinfo.sched;
        mut->trim_cinfo_old.flags = mut->trim_cinfo.flags;
        comux_cinfo_data_read_buffer(&mut->trim_cinfo_old,
                                     buffer_dptr(&mut->trim_cinfo.data),
                                     buffer_size(&mut->trim_cinfo.data));
    }

    // create an array of randomly-generated byte indexes within the chunk's
    // data segment. We'll remove these below
    uint32_t byte_indexes[mut->trim_bytes_per_step];
    for (uint32_t i = 0; i < mut->trim_bytes_per_step; i++)
    { byte_indexes[i] = RAND_UNDER(buffer_size(&mut->trim_cinfo.data)); }
    qsort(byte_indexes, mut->trim_bytes_per_step, sizeof(uint32_t), qsort_u32_cmp);

    // get a pointer to the current and old cinfo struct's data segments, and
    // iterate through to remove each selected byte
    char* dptr_new = buffer_dptr(&mut->trim_cinfo.data);
    char* dptr_old = buffer_dptr(&mut->trim_cinfo_old.data);
    uint32_t byte_counter = 0;
    uint32_t byte_index = 0;
    for (uint32_t i = 0; i < buffer_size(&mut->trim_cinfo_old.data); i++)
    {
        uint8_t still_processing_removals = byte_index < mut->trim_bytes_per_step;
        if (still_processing_removals)
        {
            // if the current "remove-this-byte" index is LESS than our current
            // index, we must be dealing with the case where two random numbers
            // generated above were equivalent. We'll just increment our index
            // to skip past the duplicate
            if (i > byte_indexes[byte_index])
            { byte_index++; }
            // if we're at the current "remove-this-byte" index, don't copy it over
            else if (i == byte_indexes[byte_index])
            {
                byte_index++;
                continue;
            }
        }
        // otherwise, we'll copy the byte to the new buffer
        dptr_new[byte_counter++] = dptr_old[i];
    }
    mut->trim_cinfo.data.size = byte_counter;
    mut->trim_cinfo.len = byte_counter;

    // we don't need to re-write the head portion to the output buffer, but we
    // DO need to reset the buffer's size so we can re-write all the bytes that
    // were shifted down in the above trimming operation (the affected chunk
    // and the bytes that follow it)
    mut->tbuff.size = buffer_size(&mut->tbuff_head);

    // write the modified chunk header
    mut->tbuff.size += comux_cinfo_write_buffer(&mut->trim_cinfo,
                            buffer_dptr(&mut->tbuff) + buffer_size(&mut->tbuff),
                            old_size);
    // write the modified chunk data
    mut->tbuff.size += comux_cinfo_data_write_buffer(&mut->trim_cinfo,
                            buffer_dptr(&mut->tbuff) + buffer_size(&mut->tbuff),
                            old_size);
    mut->trim_cinfo_old_size = buffer_size(&mut->tbuff);

    // write the bytes that follow the trimmed chunk, if there are any
    if (buffer_size(&mut->tbuff_tail) > 0)
    {
        buffer_appendn(&mut->tbuff, buffer_dptr(&mut->tbuff_tail),
                       buffer_size(&mut->tbuff_tail));
    }

    dlog_write(&mlog, STAB_TREE1 "removed %lu chunk data byte(s). "
               "Trimmed chunk down to %lu bytes.",
               buffer_size(&mut->trim_cinfo_old.data) - byte_counter,
               buffer_size(&mut->trim_cinfo.data));
    *outbuff = buffer_dptr(&mut->tbuff);
    return buffer_size(&mut->tbuff);
}

// Post-trimming API call that tells the mutator if trimming was a success
// (success meaning: by trimming, the exact same execution path was still
// achieved).
int afl_custom_post_trim(gurthang_mut_t* mut, uint8_t success)
{
    char* flog_color = success ? C_GOOD : C_BAD;
    flog_write(&mlog, "trimming %s%s%s.",
               LOG_NOT_USING_FILE(&mlog) ? flog_color : "",
               success ? "succeeded" : "failed. Resetting back to previous case",
               LOG_NOT_USING_FILE(&mlog) ? C_NONE : "");
    
    // if the last trimming stage didn't reproduce the same behavior, then
    // we want to reset back to the last-known good state. We'll do this via
    // mut->trim_cinfo_old.
    if (!success)
    {
        mut->trim_cinfo.len = mut->trim_cinfo_old.len;
        buffer_reset(&mut->trim_cinfo.data);
        buffer_appendn(&mut->trim_cinfo.data, buffer_dptr(&mut->trim_cinfo_old.data),
                       buffer_size(&mut->trim_cinfo_old.data));
    }
    
    // update mutator-internal fields
    mut->trim_count++;
    mut->trim_succeeded = success;
    mut->trim_success_count += success ? 1 : 0;

    // compute how far along we are and whether or not it's time to check how
    // successful trimming has been (we'll need this below). We'll stop to
    // check after either:
    //  - We've finished 25% of the trimming steps, OR
    //  - We've finished 100 trims
    // Whichever comes first!
    float trim_progress = (float) mut->trim_count / (float) mut->trim_steps;
    uint8_t check_success = mut->trim_count >= 100 || trim_progress >= 0.25;

    // this function returns the current trimming step we're on, out of the
    // total trimming steps. If we're not getting much success in trimming
    // we'll return the maximum index to tell AFL++ to give up early.
    // This will prevent us from spending too much time trimming if nothing
    // useful will come of it.
    float ratio = (float) mut->trim_success_count / (float) mut->trim_count;
    float ratio_threshold = 0.1;
    if (check_success && ratio < ratio_threshold)
    {
        dlog_write(&mlog, STAB_TREE1 "less than a %.0f%% success rate "
                   "(%.0f%%) after %d trim steps. Bailing out early.",
                   ratio_threshold * 100.0, ratio * 100.0, mut->trim_count);
        return mut->trim_steps;
    }

    // on the last step, print a conclusion
    if (mut->trim_count == mut->trim_steps)
    {
        dlog_write(&mlog, STAB_TREE2 "concluded trimming with %d "
                   "successes and %d failures (success rate of %.0f%%).",
                   mut->trim_success_count,
                   mut->trim_steps - mut->trim_success_count,
                   ratio * 100.0);
        dlog_write(&mlog, STAB_TREE1 "reduced chunk by %d bytes.",
                   mut->trim_success_count);
    }

    // otherwise, we'll just return the current trim index
    return mut->trim_count;
}

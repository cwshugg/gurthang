This document lists and describes all the environment variables that can be set to tune gurthang.

# Mutator Environment Variables

### `GURTHANG_MUT_LOG`

This can be set to enable logging of the mutator's actions. Set it to one of the following:

* `1` - logging will go to stdout.
* `2` - logging will go to stderr.
* `path/to/a/file.txt` - logging will be written to a file.

### `GURTHANG_MUT_DEBUG`

This can be enabled along with `GURTHANG_MUT_LOG` in order to enable additional debugging log messages. This can only be enabled while `GURTHANG_MUT_LOG` is enabled.

### `GURTHANG_MUT_FUZZ_MIN`

This sets the minimum number of fuzzing executions performed for a single test case. Realistically, the number of fuzzing executions will fluxuate for certain inputs (the mutator will increase the count for "interesting" inputs and decrease it for "not-as-interesting" inputs), but the mutator prevents it from dipping below a threshold. The default threshold is 512, and this environment variable can be used to adjust that.

### `GURTHANG_MUT_FUZZ_MAX`

Sets the *maximum* number of fuzzing executions performed for a single test case, similarly to `GURTHANG_MUT_FUZZ_MIN`. The default maximum threshold is 32768.

### `GURTHANG_MUT_TRIM_MAX`

This sets the maximum number of trimming steps during a single AFL++ trimming stage. Trimming is an expensive operation, so you can choose to lower this value (default is 2500) to speed things along. You'll just end up with bigger test cases.

Set this to -1 to have *no* limit.

### `GURTHANG_MUT_DICT`

Set this file to a file path (or multiple file paths separate by a comma) to point the mutator at dictionary files. If at least one dictionary is specified with this environment variable, the mutator will, in addition to its other mutation strategies,
perform the following mutations:

* **Dictionary Swap**: for a mutation, the mutator might decide to locate a word within the given dictionary within a test case, and swap it out for another word in that dictionary.
* (More mutations may be added that deal with the dictionary in the future).

These dictionaries should have one word per line, with no empty lines. The parser doesn't account for windows line endings, either, so make sure you don't have any carriage returns (`\r`) if you don't want them placed into the mutated inputs.

```bash
# example usage of GURTHANG_MUT_DICT:
GURTHANG_MUT_DICT=/path/to/my/dictionary.txt
GURTHANG_MUT_DICT=./dict1.txt,./dict2.txt,./dict3.txt
```

# Preload Library Variables

### `GURTHANG_LIB_LOG`

This can be set to enable logging of the `LD_PRELOAD` library's actions. It works just like the mutator's log. Set it to one of the following:

* `1` - logging will go to stdout.
* `2` - logging will go to stderr.
* `path/to/a/file.txt` - logging will be written to a file.


### `GURTHANG_LIB_SEND_BUFFSIZE`

This can be adjusted to set the size of the buffer used by the preload library's `send()` system call when attempting to send bytes to the target server. Default is 2048.

### `GURTHANG_LIB_RECV_BUFFSIZE`

This can be adjusted to set the size of the buffer used by the preload library's `recv()` system call when attempting to receive response bytes from the target server. Default is 2048.

### `GURTHANG_LIB_NO_WAIT`

This can be enabled (set to any value) to turn on **NO_WAIT** mode. When this is enabled, the controller library thread will spawn all chunk threads before waiting for any of them to complete. Once all are spawned, it will `pthread_join()` them.

This is different from the default behavior: the controller thread will spawn a chunk thread, wait for it to exit, spawn the next one, wait for it to exit, etc.

```
Controller thread's typical behavior:
    1. Spawn chunk thread 1.
    2. Wait for chunk thread 1 to exit.
    3. Spawn chunk thread 2.
    4. Wait for chunk thread 2 to exit.
    ...
    X-1. Spawn chunk thread N.
    X. Wait for chunk thread N to exit.
    X+1. Done!

Controller thread's behavior with NO_WAIT mode enabled:
    1. Spawn chunk thread 1.
    2. Spawn chunk thread 2.
    3. Spawn chunk thread 3.
    ...
    X-1. Spawn chunk thread N.
    X. Wait for all threads to exit.
    X+1. Done!
```

**NO_WAIT** mode is not ideal for fuzzing. Thanks to the unpredictability of thread scheduling, by enabling **NO_WAIT** mode, each subsequent run of the same input file will be much less deterministic. But it's an interesting feature that might be handy.

### `GURTHANG_LIB_EXIT_IMMEDIATE`

Set this environment variable to *anything* to change how gurthang exits the
process after the controller thread is finished. By default, `exit()` will be
invoked, which will run any exit handlers in the target program, if any.
However, enabling this environment variable will force gurthang to use
`_exit()` instead to somewhat-immediately exit the process.

I've found this to be a useful feature when dealing with a target program
that expects an "internal" thread (not the gurthang controller thread) to
call `exit()` in order to properly run exit handlers.

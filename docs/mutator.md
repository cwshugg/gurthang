This document describes the details of gurthang's AFL++ custom mutator module. Details on the API for custom mutators can be found [at this link](https://aflplus.plus/docs/custom_mutators/).

AFL++'s custom mutator support allows for a programmer to write a shared object file (`.so`) and dynamically load it into AFL++ at runtime. AFL++ then can invoke this mutator to fuzz test cases. With the `AFL_CUSTOM_MUTATOR_ONLY` environment variable enabled, one can restrict AFL++ to *only* using the custom mutator. This is handy when writing a mutator that deals with a specific grammar or file format that might otherwise be clobbered by AFL++'s random bitflips and other mutations. Gurthang's mutator reads comux files, and as such requires `AFL_CUSTOM_MUTATOR_ONLY` to be set.

# Test Case Inspection

## Deciding to use a test case

Part of the custom mutator interface is `afl_custom_queue_get`. This function, if implemented by the mutator, is invoked when AFL++ first examines a test case in the to-be-executed queue. The mutator can examine the file itself and return a value to tell AFL++ whether or not to attempt fuzzing the input case, or to simply throw it away.

Gurthang implements this function as a way to ensure the validity of a comux file. It opens the queued file for reading and attempts to parse the comux header and comux chunks, checking for invalid header fields. The mutator decides the throw the test case away if one of the following is true:

* Parsing the main comux header fails
    * (i.e. too few bytes, incorrect magic bytes, incorrect flag bits are set, etc.)
* A comux chunk's header cannot be parsed
    * (i.e. too few bytes, incorrect chunk header fields, etc.)
* A comux chunk's data isn't the correct length as specific in the chunk's header

The chunk data itself is not examined. Only the main comux header and the headers for each chunk are examined for validity. If all the checks pass, the mutator gives AFL++ the "all clear" to try fuzzing with this test case.

### Not Foolproof

It should be noted that this was originally implemented as a check to ensure comux header values weren't tampered with. It's capable of detecting *some* issues that might arise in a badly-formatted comux file, but not all of them. This only becomes an issue if the `AFL_CUSTOM_MUTATOR_ONLY` environment variable is *not* enabled when fuzzing with gurthang's mutator. Should that happen, it's possible some comux header data would be clobbered by AFL++'s built-in mutations.

An example: say AFL++ flips a few bits in part of the comux file that represents the scheduling value for a specific comux chunk. The bytes that make up the chunk's `sched` field might be changed from 3 to something huge, like 395874. This would *not* be detected by the mutator's `afl_custom_queue_get`. It would, however, produce some unexpected behavior when parsed by the gurthang `LD_PRELOAD` library.

In short, it's not smart to use this mutator without enabling `AFL_CUSTOM_MUTATOR_ONLY`.

## Deciding how many times to mutate a test case

Custom AFL++ mutators are given the ability to examine a test case and tell AFL++ exactly how many times the test case should be mutated and tried against the target input. This could be useful for some mutators that might want to prioritize specific test cases over others by giving them more fuzzing attempts. Gurthang's mutator does this through the `afl_custom_fuzz_count` function.

The gurthang mutator prioritizes comux files with *more* connections and/or *more* chunks. The idea here is that a comux file with more *connections*  will create more work for the target server, potentially uncovering newer behavior. More connections may also lead to more concurrency-related bugs. A comux file with more *chunks* may mean payloads to be sent across a single connection are split up among chunks, and thus there may be delays between sending the payload chunks. This also may lead to interesting behavior.

A minimum and maximum are stored internally and used to compute the test case's "fuzz count." (These minimum and maximums can be set by the user at runtime with `GURTHANG_MUT_FUZZ_MIN` and `GURTHANG_MUT_FUZZ_MAX`.) Additionally, the previous "fuzz count" is taken into account with the following rules:

* **If the comux file has multiple connections:** increase the fuzz count by a multiple of the number of connections in the file (or at least 3).
* **If the comux file does NOT have multiple connections:** cut the fuzz count in half.
* **If the comux file has more chunks than it does connections:** increase the fuzz count by a multiple of the difference between the two (or at least 3).
* **If the comux file does NOT have more chunks than connections:** cut the fuzz count in half.

After the above criteria have been evaluated, the computed fuzz count is adjusted to ensure it falls within the minimum and maximum.

# Fuzzing Process

To perform a single fuzzing step for one test case (i.e. a single run of the mutator's `afl_custom_fuzz` function), the following is done:

## Step 1 - Parse

1. Parse the comux header
    * *On a parsing failure, leave it untouched*
2. For every chunk in the comux file:
    1. Parse the chunk's header
        * *On a parsing failure, leave the test case untouched*
    2. Read the chunk's data into memory
        * *On a reading failure, leave the test case untouched*

Parsing failures are expected *not* to happen, thanks to the mutator's implementation of `afl_custom_queue_get`. `afl_custom_queue_get` is invoked prior to the main fuzzing function, so any parsing errors will remove the test case from AFL++'s consideration. These parsing failures are implemented solely to be thorough with error checking.

## Step 2 - Mutate

At this point, every comux chunk has been parsed and read into memory. First, a "mutation strategy" is selected from the list described below. Once selected, it searches for a suitable chunk (randomly) in the comux file and performs a single mutation on it. If a suitable chunk can't be found, another strategy is chosen and the process repeats.

Eventually, a single mutation is performed on a *single* comux chunk in the file.

## Step 3 - Write-Back

After mutation has occurred, everything must be written back out to memory. Writing occurrs in the same order as parsing:

1. Write the comux header
    * *On a writing failure, leave the test case untouched*
2. For every chunk in the comux file (including the mutated chunk):
    1. Write the chunk's header
        * *On a writing failure, leave the test case untouched*
    2. Write the chunk's data
        * *On a writing failure, leave the test case untouched*

At the conclusion of writing, a buffer is filled up with the mutated test case's bytes and returned to AFL++. The fuzzer takes this and sends it to the target program via stdout/stdin.

# Mutation Strategies

Gurthang's mutator employs various strategies to mutate a test case. They're described below.

## Havoc Mutation

A special mutation set apart from the others is the `afl_custom_havoc_mutation` (havoc mutation). The idea behind a "havoc" mutation is to perform some random bitwise/bytewise operation on the target, without any regard to its structure. This function simply invokes the existing mutation routine and forces the selection of the `CHUNK_DATA_HAVOC` strategy (described below).

In tandem with `afl_custom_havoc_mutation` is `afl_custom_havoc_mutation_probability`. This function can optionally be implemented by the mutator to tell AFL++ how often it should invoke the custom mutator's havoc mutation as opposed to its own. Gurthang's mutator simple returns `100` from this function, indicating to AFL++ it should *always* invoke the custom havoc mutation function.

## `CHUNK_DATA_HAVOC`

The havoc mutation strategy selects a single random chunk in the comux file and performs a single AFL++-like havoc mutation on it. This is implemented simply by invoking the `surgical_havoc_mutate` function provided as a helper method for custom mutators. Gurthang's mutator simply provides it with the chunk's data and instructs it to work within the data's bounds. The function chooses some random bitwise/bytewise operation and performs it on a random bit/byte.

## `CHUNK_DATA_EXTRA`

This mutation is similar to the havoc mutation but implements a small number of "extra" mutations that might prove to be useful. They are described below.

* **Reverse Bytes:** this mutation selects a random range of bytes in a random comux chunk and reverses their order.
* **Swap Two Bytes:** this mutation selects two random bytes in the chunk's data buffer and swaps their positions.

## `CHUNK_SCHED_BUMP`

This mutation was implemented to intentionally modify the `sched` field for comux chunks. The idea behind it is that by modifying when chunk's are scheduled, the gurthang `LD_PRELOAD` library will establish connections or send chunks of data to the target server in different orders. The inclusion of a `sched` field in the comux file format was for this exact purpose: so the fuzzer could modify the connection order on the fly.

This mutation first searches for a suitable chunk with enough "wiggle" room to have its `sched` value updated to make a difference. A suitable chunk has the following properties:

1. The chunk must have a scheduling value that, if increased or decreased, will still maintain the same relative ordering within its own connection.
2. The chunk must, by having its `sched` increased or decreased, be scheduled differently than it was before, with respect to its neighboring chunks for *other* connections.

Take the following example:

```
CHUNK       CONN_ID     SCHED
-----------------------------
C-0         0           1
C-1         1           0
C-2         0           2

This means...
- C-1 is sent first
- C-0 is sent second
- C-2 is sent third
```

Let's say we select `C-0` for a "sched bump". It belongs to connection 0. So does `C-2`. `C-2` is scheduled *after* `C-0`, so we want to make sure that remains the same. Why? It's possible `C-2` likely contains part of a payload that must arrive *last* along connection 0, after `C-0`'s payload. Changing this order might cause hangs in the target program as it awaits structured input in the expected order.

So, we cannot increase `C-0`'s scheduling value up to 3. We could increase it up to 2, but this would not change the order in which the chunks are sent. (The order would still be: `C-1 --> C-0 --> C-2`.) We *can*, however, decrease it to 0. This will change the order in which the chunks are sent while maintaining the order relative to connection 0:

```
CHUNK       CONN_ID     SCHED
-----------------------------
C-0         0           0
C-1         1           0
C-2         0           2

This means...
- C-0 is sent first
- C-1 is sent second
- C-2 is sent third
```

If a suitable chunk can't be found, a different mutation strategy is tried. Otherwise, the chosen chunk has its scheduling value randomly updated to meet the second condition above.

## `CHUNK_SPLIT`

This mutation selects a random suitable chunk and splits it into two separate chunks. Splitting chunks may open the door for more future `SCHED_BUMP` mutations, allowing payloads to be sent to the target server at different times and in different order. A "suitable" chunk is selected in the same way as described in the `CHUNK_SCHED_BUMP` mutation - there must be enough "wiggle room" among the selected chunk's same-connection neighbors to split it while maintaining the same payload delivery order for the corresponding connection.

If a suitable chunk cannot be found, a different strategy is selected.

## `CHUNK_SPLICE`

This mutation does the *opposite* of `CHUNK_SPLIT`. It selects two neighboring same-connection chunks and combines them into one chunk, randomly choosing a new scheduling value.

If no two chunks can be found, a different strategy is selected.

## `CHUNK_DICT_SWAP`

The gurthang mutator supports the use of dictionaries in the form of text files. Multiple dictionaries can be specific at runtime via the `GURTHANG_MUT_DICT` environment variable. Each dictionary is required to contain one word per line, like so:

```
word1
word2
word3
word4
```

These dictionaries, if given, are used in the "dictionary swap" mutation. This mutation works by choosing a random chunk and searching the chunk's data for occurrence of random entries in each dictionary. If a match it found, the word is swapped for a *different* random word from the same dictionary.

Takes this example of a comux chunk with a partially-complete HTTP message:

```
GET /index.html HTTP/1.1
Content-Le
```

If the user supplied a dictionary that contained HTTP request methods (such as `GET`, `POST`, `PUT`, `DELETE`, etc.), a dictionary swap mutation might discover the `GET` keyword in the chunk and swap it for another entry:

```
PUT /index.html HTTP/1.1
Content-Le
```

The motivation behind this mutation is support more grammar-friendly inputs. It's extremely far from a mutator that knows *exactly* how the structure/grammar of an input type is formatted, but it's a step in the right direction, and one that could create interesting behavior when fed to the target program.

# Test Case Trimming

AFL++ custom mutators can optionally implement test case trimming. This is AFL++'s way of carefully reducing the size of a test case such that it still invokes the same behavior in the target program. In order to prevent AFL++'s built-in trimming methods from clobbering comux header information, the gurthang mutator implements custom trimming procedures.

The AFL++ trimming procedure works by first choosing a number of trimming steps to perform for a test case. Then, for each trimming step, random bytes are removed and re-executed with the target program. If the same behavior was invoked, the trimming step succeeded. Otherwise, the trimming step failed.

The gurthang mutator's trimming procedures work like this:

1. Parse the test case and determine how many chunks are present.
2. Choose one chunk at random (we'll call this chunk `C`).
3. Choose a set number of bytes to remove during each step (`N`)
    * (The number of bytes is chosen proportional to `C`'s data size.)
4. For each trimming step:
    1. Remove `N` random bytes from `C`'s data segment.
    2. If trimming succeeded, use the new version for the next trimming step.
    3. If trimming failed, reset `C`'s data segment back to what it was prior to this trimming step.
        * If, after 100 trims of 25% of the total trimming steps (whichever comes first), there is a less than 10% success rate, give up on trimming.


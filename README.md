This is **gurthang**, my web server fuzzing harness, composed of an `LD_PRELOAD` library and AFL++ custom mutator module. Turin Turambar wielded Gurthang, the sword called the "Iron of Death", and slayed Glaurung the dragon. Maybe, just maybe, AFL++ can wield this harness to slay some web servers.

<img align="right" src="./docs/images/gurthang_light.png" width=385>

# Files

The source files are organized like so:

* `src/` contains the AFL++ mutator, the `LD_PRELOAD` library, and the implementation of the **connection-multiplexing** file format.
* `tests/` contains a unit testing header file, along with a series of testing modules I've written to test smaller parts of my code.
* `scripts/` contains any scripts I wanted to save to make development and fuzzing easier.
* `dicts/` contains example dictionaries that can be plugged into gurthang in order to make use of dictionary-based mutations.
* `docs/` contains markdown documentation and images.

## Setup

To set things up, pull and build AFL++. Copy the file path to its `include/` directory (for example: `/home/cwshugg/AFLplusplus/include`), and drop it into the `AFLPP_INCLUDE` makefile variable:

```bash
# ... in the makefile ...

# AFL++ variables
AFLPP_INCLUDE=/home/cwshugg/AFLplusplus/include

# ...
```

After that, build your web server and use the following environment variables to utilize gurthang to fuzz it:

```bash
AFL_PRELOAD=${gurthang_repo}/gurthang-preload.so \
AFL_CUSTOM_MUTATOR_LIBRARY=${gurthang_repo}/gurthang-mutator.so \
AFL_CUSTOM_MUTATOR_ONLY=1 \
${afl_fuzz} # ... other AFL arguments go here
```

See this document for [additional environment variables](./docs/environment_variables.md) gurthang supports.

# The `LD_PRELOAD` Library

To enable the fuzzing of web servers via AFL++, this project has two components: the library and the mutator. The first of these is a shared object (`.so`) library that is passed to the server via the `LD_PRELOAD` environment variable. Simply put, its job is to read a **comux** file from stdin, use it to establish internal connections to the server, and feed the input through those connections.

More details on the library can be found [here](./docs/preload.md).

# The AFL++ Custom Mutator

The `LD_PRELOAD` library controls the reading and parsing of these **comux** files (described below), whereas gurthang's AFL++ custom mutator module is responsible for performing mutations on those comux files. Such mutations might be:

* Typical AFL++ bit/byte operations on comux payloads (bitflip, byte swap, byte reversal, etc.)
* Change the order in which the connections are established
* Change the order in which the payloads are sent
* Split up the payloads to send less data at one time
* Combine two payloads to send more data at one time

In essence, this special file format allows the mutator to decide not only *what* is getting sent to the target server, but *how* it is sent to the server. More details on the mutator can be found [here](./docs/mutator.md).

# Comux

The **Co**nnection **Mu**ltiple**x**ing protocol implemented in `src/comux/` allows you to format a single file in a way that defines the content to be sent to a remote web server across *multiple* concurrent socket connections. As mentioned above, the AFL++ mutator and `LD_PRELOAD` library use this protocol enable AFL++ to establish multiple connections to the target web server, and send multiple payloads through those connections, in a single execution of the fuzzer.

See [this page](./docs/comux.md) for a full description.


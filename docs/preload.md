This document describes the details of how the `LD_PRELOAD` library produced by compiling this source code works.

# Overloading the `accept()` System Call

Because the target program of this project is a web server, the `LD_PRELOAD` library implements its own version of the `accept()` system call. When the server calls `accept()` to receive a new client connection, the library's version is executed instead. Briefly put, this "injected" version of the system call does the following:

1. Runs library initialization code (only done for the first call).
2. Spawns a single **controller thread** (only done for the first call). More on this thread later.
3. Calls the *real* `accept()` system call, and returns its value.

# Threads

The library code spawns multiple threads. They're described below.

## The Controller Thread

When the first call to `accept()` is made by the target server, the library code spawns a single thread. This is the main library thread. Its job is to read from the standard input stream (through which a [comux file](./comux.md) is fed) and parse the contents to understand the number of socket connections to be made, the number of "chunks" of data to process, which connections those chunks go to, and when to send each of them.

Once parsed, it begins spawning one thread for each chunk present in the file. Each of these threads are **chunk threads**, and are responsible for handling a single chunk on one of the connections to the server. See below for details on exactly what they do.

## Chunk Threads 

Chunk threads handle the connection-establishing and data-sending logic. The controller thread packs information together into a single struct on the heap and passes it to the new thread as its input. The information a chunk thread receives is:

* Metadata on the chunk of data it's responsible for
    * Which connection it should be sent to
    * How many bytes of data it's comprised of
    * Where in stdin (offset) the data can be found
* Whether or not this chunk is the *final* chunk for a connection.

Once it has this information, a chunk thread follows this process:

1. Reference the connection table and determine the correct socket file descriptor to use, based on the chunk's connection ID. (See below)
    1. If it finds out its assigned connection is no longer valid, the thread exits.
2. Seek to the correct location within stdin and load the chunk's data bytes into memory.
3. Send the data across the connection to the target server.
    1. If the target server closes the remote connection while sending, the thread will invalidate the entry in the connection table.
    2. If the chunk thread is sending the *final* chunk for a connection, the thread will shutdown the socket's write-end after sending.

# The Connection Table

This preload library works by spawning a single thread for each chunk specified in the comux file (given through stdin). Several chunks, despite being spread across the comux file, may have the same connection ID. This means that all of those chunks should be sent through the *same* connection (socket file descriptor).

In order to keep track of this between multiple threads, the library implements a table of file descriptors. When one chunk thread is trying to find the right file descriptor to use, it plugs its connection ID into the table. Depending on what it finds there (a live connection, an already-closed connection, no connection, etc.), it will either reuse the correct file descriptor from the table, store a *new* file descriptor in the table, or give up.

# Library Diagrams

A few diagrams on how this library works are illustrated below.

## Threading Diagram

The overall process of how this library's controller thread and chunk threads work together to communicate with the target server. The arrows represent the flow of execution for each thread, from top to bottom chronologically.

```
        SERVER EXECUTION                      LIBRARY EXECUTION
        ================                =============================
            |  |  |
            |  |  |
            V  V  V
        library_accept() ----------- NEW THREAD --------------------+                   ..............
            |  |  |                                                 |  <::::::::::::::: : comux file :
            |  |  |                                                 |                   :  (stdin)   :
            V  V  V                                                 V                   :............:
          real_accept()                                      parse the comux
                                                             file containing
                                                              N connections
                                                               and M chunks
                                                                    |
                                                                    |
                                                                    V
                                        +--- NEW THREAD ------ spawn chunk   
                                        |                        thread 1
                                        V                           .
                                   send data to                     :
                                     assigned                       |
                                    connection                      |
                                        |                           |
                <::::::::: data :::::::::                           |
                |                       |                           |
             server                     V                           |
             handles              if specified,                     |
             request            wait for response                   :
                |                       ?                           '                   ..........
                ::::::: response :::::> ? ????????????????????????????????????????????> : stdout :
                                        ?                           .                   :........:
                                        |                           :
                                        |                           |
                                        |                           V
                                        +---------------------> join chunk
                                                                 thread 1
                                                                    |
                                                                    |
                                                                    V
                                                               repeat for
                                                               chunks 2-M
                                                                    |
                                                                    |
                                                                    V
                                                           all chunks handled.
                                                                  exit()
```

## Connection Diagram

From the target server's perspective, it simply receives multiple connection requests, and receives data for each connection at specific times. If we represent each connection as a vertical timeline, we can visualize the idea. As an example, take a comux file that specifies the following:

* Chunk 0 goes to connection 0. Scheduling = 0
* Chunk 1 goes to connection 1. Scheduling = 1
* Chunk 2 goes to connection 0. Scheduling = 2
* Chunk 3 goes to connection 3. Scheduling = 3
* Chunk 4 goes to connection 2. Scheduling = 4
* Chunk 5 goes to connection 1. Scheduling = 5
* Chunk 6 goes to connection 2. Scheduling = 6
* Chunk 7 goes to connection 0. Scheduling = 7
* Chunk 8 goes to connection 1. Scheduling = 10
* Chunk 9 goes to connection 1. Scheduling = 8
* Chunk 10 goes to connection 2. Scheduling = 9

So, there are eleven chunks spread across four connections. Most of them are scheduled in the order they appear in the file, but for the sake of demonstration, the last three have out-of-order scheduling values. The chunks would be sent to the target server like this:

```
    SCHEDULE    CONNECTION 0        CONNECTION 1        CONNECTION 2        CONNECTION 3
    ========    ========================================================================
                     |                   |                   |                   |
                +---------+              |                   |                   |
       0        | chunk 0 |              |                   |                   |
                +---------+              |                   |                   |
                     |              +---------+              |                   |
       1             |              | chunk 1 |              |                   |
                     |              +---------+              |                   |
                +---------+              |                   |                   |
       2        | chunk 2 |              |                   |                   |
                +---------+              |                   |                   |
                     |                   |                   |              +---------+
       3             |                   |                   |              | chunk 3 |
                     |                   |                   |              +---------+
                     |                   |              +---------+              |
       4             |                   |              | chunk 4 |              |
                     |                   |              +---------+              |
                     |              +---------+              |                   |
       5             |              | chunk 5 |              |                   |
                     |              +---------+              |                   |
                     |                   |              +---------+              |
       6             |                   |              | chunk 6 |              |
                     |                   |              +---------+              |
                +---------+              |                   |                   |
       7        | chunk 7 |              |                   |                   |
                +---------+              |                   |                   |
                     |              +---------+              |                   |
       8             |              | chunk 9 |              |                   |
                     |              +---------+              |                   |
                     |                   |              +----------+             |
       9             |                   |              | chunk 10 |             |
                     |                   |              +----------+             |
                     |              +---------+              |                   |
       10            |              | chunk 8 |              |                   |
                     |              +---------+              |                   |
                     |                   |                   |                   |
                ========================================================================
```

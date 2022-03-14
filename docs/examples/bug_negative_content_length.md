# Bug Deep-Dive: Negative Content Length

Another bug that proved interesting as I was examining some of the submitted web servers & fuzzing reuslts was a segmentation fault (SIGSEGV) on a call to `realloc()`. This occurred while attempting to resize a heap-allocated buffer.

## Input Comux File

The comux file is another one-connection-one-chunk file, which makes debugging easier. Its contents are:

```bash
$ comux -s -i -v ../fuzz/fuzz2/crashes/id:000000*
* COMUX [version: 0] [num_connections: 1] [num_chunks: 1]
* CHUNK 0: conn_id=0, data_length=152, scheduling=0, flags=0x1
POST /api/login HTTP/1.1
Content-Length:-47
Accept-Encoding: identity
Host: hornbeam.rlogin:29826

{"username": "user0", "password": "RVIfLBcbfn"}
```

Everything looks fairly normal - this is a valid HTTP message. Except one thing. The `Content-Length` header has a negative value. We could probably pack it up here and assume this negative value was used to deal with heap memory when a positive value was expected. But for the sake of exploration, we'll still dive into it.

## Reproducing the Crash

As a sanity check, we'll rerun the buggy server with the same comux input and ensure we receive the SIGSEGV:

```bash
$ LD_PRELOAD=${GURTHANG_LIB} ./server -p 13650 < ../fuzz/fuzz2/crashes/id:000000*
Using port 13650
Accepted connection from ::1:32894
Segmentation fault (core dumped)
```

There it is. Time to debug.

## Checking with Valgrind

Valgrind has lots to say about this particular bug. Several uninitialized value uses, conditional jump via uninitialized values, and an invalid free, all dealing with a buffer used by the server to keep track of the current request/response pair. Here is a snippet of the long output:

```bash
$ LD_PRELOAD=$GURTHANG_LIB valgrind ./server -p 13650 < ../fuzz/fuzz2/crashes/id:000000*
# ... LOTS of output ...
==3866993== Conditional jump or move depends on uninitialised value(s)
==3866993==    at 0x4C3C049: realloc (vg_replace_malloc.c:1437)
==3866993==    by 0x4022DA: buffer_ensure_capacity (buffer.h:76)
==3866993==    by 0x402347: buffer_append (buffer.h:88)
==3866993==    by 0x4023BA: buffer_appends (buffer.h:103)
==3866993==    by 0x402894: http_add_header (http.c:168)
==3866993==    by 0x402DAF: send_error (http.c:285)
==3866993==    by 0x1900040345B: ???
==3866993==    by 0x40398F: http_handle_transaction (http.c:571)
==3866993==    by 0x401A58: new_client (main.c:43)
==3866993==    by 0x5B6817E: start_thread (in /usr/lib64/libpthread-2.28.so)
==3866993==    by 0x5DB9D82: clone (in /usr/lib64/libc-2.28.so)
==3866993== 
==3866993== Invalid free() / delete / delete[] / realloc()
==3866993==    at 0x4C3C096: realloc (vg_replace_malloc.c:1437)
==3866993==    by 0x4022DA: buffer_ensure_capacity (buffer.h:76)
==3866993==    by 0x402347: buffer_append (buffer.h:88)
==3866993==    by 0x4023BA: buffer_appends (buffer.h:103)
==3866993==    by 0x402894: http_add_header (http.c:168)
==3866993==    by 0x402DAF: send_error (http.c:285)
==3866993==    by 0x1900040345B: ???
==3866993==    by 0x40398F: http_handle_transaction (http.c:571)
==3866993==    by 0x401A58: new_client (main.c:43)
==3866993==    by 0x5B6817E: start_thread (in /usr/lib64/libpthread-2.28.so)
==3866993==    by 0x5DB9D82: clone (in /usr/lib64/libc-2.28.so)
==3866993==  Address 0x54534f50 is not stack'd, malloc'd or (recently) free'd
==3866993== 
==3866993== Use of uninitialised value of size 8
==3866993==    at 0x4022E2: buffer_ensure_capacity (buffer.h:76)
==3866993==    by 0x402347: buffer_append (buffer.h:88)
==3866993==    by 0x4023BA: buffer_appends (buffer.h:103)
==3866993==    by 0x402894: http_add_header (http.c:168)
==3866993==    by 0x402DAF: send_error (http.c:285)
==3866993==    by 0x1900040345B: ???
==3866993==    by 0x40398F: http_handle_transaction (http.c:571)
==3866993==    by 0x401A58: new_client (main.c:43)
==3866993==    by 0x5B6817E: start_thread (in /usr/lib64/libpthread-2.28.so)
==3866993==    by 0x5DB9D82: clone (in /usr/lib64/libc-2.28.so)
# ... MORE output ...
```

## Deducing the Bug

I tested this with a corrected version of the crash file with the *correct* `Content-Length`, and none of this showed up. Considering that, I took a look at the code that deals with *using* the content length after it's been parsed as -47:

```c
bool valid_cookie = false;
if (!http_process_headers(&ta, &valid_cookie))  // <-- this is where Content-Length is parsed
    return false;

if (ta.req_content_len > 0) {                   // <-- the message body isn't read on a <= 0 content length
    int rc = bufio_read(self->bufio, ta.req_content_len, &ta.req_body);
    if (rc != ta.req_content_len)
        return false;
}
```

As we can see, immediately after `Content-Length` is parsed (to be -47, in this case), the message body is read *if any only if* the content length is greater than 0. In our case, it's less than 0, so the message body is *not* read.

It's because of this we see so many uninitialized value uses in Valgrind's error output. The crash-inducing input sends a HTTP message requesting `/api/login`, where the programmer implemented code that attempts to parse the message body's contents. The bug? The programmer never checked if there was *no* message body. So it's effectively reading from a buffer that was never initialized on the stack or the heap.


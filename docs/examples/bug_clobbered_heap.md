# Bug Deep-Dive: Clobbered Heap

In the fall 2021 semester at Virginia Tech, I gained IRB approval to invite students taking CS 3214 (Computer Systems) to use AFL++ and gurthang to fuzz their HTTP web servers, developed as the final project for the course.

Lots of students agreed to participate, and I collected their source code and fuzzer output for analysis. Lots of bugs were found, most of them related to faulty string parsing code. One interseting bug found by the fuzzer is discussed here.

## Input Comux File

AFL++ reported that the student's server crashed with a SIGABRT (signal number 6). The input file that caused the crash is a simple one-chunk-one-connection comux file:

```bash
$ comux -s -v -i ../fuzz/fuzz1/crashes/id:000000*
* COMUX [version: 0] [num_connections: 1] [num_chunks: 1]
* CHUNK 0: conn_id=0, data_length=153, scheduling=0, flags=0x1
POST /api/login HTTP/1.1
Host: hornbeam.rlogin:28756
Accept-Encoding: identity
Content-Length: 48

{"username": "user0", "password": "thep
```

We can see the file has a single connection represented by a single chunk. The chunk is 153 bytes long, and its content is a valid HTTP message:

```
POST /api/login HTTP/1.1
Host: hornbeam.rlogin:28756
Accept-Encoding: identity
Content-Length: 48

{"username": "user0", "password": "thep
```

It's a `POST` request to `/api/login`, one of the endpoints the students were required to support. The code written expects a valid JSON object with `"username"` and `"password"` fields, both strings. However, the payload within this comux file specifies a `Content-Length` of 48, even though the actual content is only 39 bytes long. We can see the JSON object in the message body is missing its ending.

## Reproducing the Crash

This comux file introduces the possibility of an out-of-bounds write on the server's end. Running this with the gurthang `LD_PRELOAD` library yields our SIGABRT the fuzzer reported earlier:

```bash
$ LD_PRELOAD=${GURTHANG_LIB} ./server -p 13650 < ../fuzz/fuzz1/crashes/id:000000*
Using port 13650
Waiting for client...
Accepted connection from ::1:60760
Waiting for client...
server: malloc.c:2396: sysmalloc: Assertion `(old_top == initial_top (av) && old_size == 0) || ((unsigned long) (old_size) >= MINSIZE && prev_inuse (old_top) && ((unsigned long) old_end & (pagesize - 1)) == 0)' failed.
Aborted (core dumped)
```

Based on what's being asserted in `malloc.c` on line 2396, it's a pretty good guess a heap block's boundary tag was overwritten by the server's attempt to copy 48 bytes from a 39-byte buffer.

## Checking with Valgrind

A good next step would be to run this same setup through Valgrind to see if it's able to detect what went wrong.

```bash
$ LD_PRELOAD=${GURTHANG_LIB} valgrind ./server -p 13650 < ../fuzz/fuzz1/crashes/id:000000*
HTTP/1.1 400 Bad Request
Server: CS3214-Personal-Server
Content-Type: application/json
Content-Type: text/plain
Content-Length: 65

Error parsing body on line 1: premature end of input near '"thep'
983224== Command: ./server -p 13650
==1983224== 
Using port 13650
Waiting for client...
Accepted connection from ::1:60828
Waiting for client...
==1983224== Thread 4:
==1983224== Invalid write of size 1
==1983224==    at 0x4C3D3C4: strncpy (vg_replace_strmem.c:599)
==1983224==    by 0x404479: http_handle_transaction (http.c:705)
==1983224==    by 0x4046C6: perform_http (http.c:764)
==1983224==    by 0x5B6817E: start_thread (in /usr/lib64/libpthread-2.28.so)
==1983224==    by 0x5DB9D82: clone (in /usr/lib64/libc-2.28.so)
==1983224==  Address 0x65f3348 is 0 bytes after a block of size 40 alloc'd
==1983224==    at 0x4C3BE4B: calloc (vg_replace_malloc.c:1328)
==1983224==    by 0x404456: http_handle_transaction (http.c:704)
==1983224==    by 0x4046C6: perform_http (http.c:764)
==1983224==    by 0x5B6817E: start_thread (in /usr/lib64/libpthread-2.28.so)
==1983224==    by 0x5DB9D82: clone (in /usr/lib64/libc-2.28.so)
==1983224== 
==1983224== 
==1983224== HEAP SUMMARY:
==1983224==     in use at exit: 400 bytes in 3 blocks
==1983224==   total heap usage: 29 allocs, 26 frees, 19,664 bytes allocated
==1983224== 
==1983224== LEAK SUMMARY:
==1983224==    definitely lost: 0 bytes in 0 blocks
==1983224==    indirectly lost: 0 bytes in 0 blocks
==1983224==      possibly lost: 288 bytes in 1 blocks
==1983224==    still reachable: 112 bytes in 2 blocks
==1983224==         suppressed: 0 bytes in 0 blocks
==1983224== Rerun with --leak-check=full to see details of leaked memory
==1983224== 
==1983224== For lists of detected and suppressed errors, rerun with: -s
==1983224== ERROR SUMMARY: 8 errors from 1 contexts (suppressed: 0 from 0)
```

Valgrind flagged an invalid write of one byte in a call to `strncpy()` within `http.c` on line 705. Perhaps this is pointing to the source of the heap clobbering. We can use GDB to find out.

## Checking with GDB

Running the same setup again, this time in GDB, allows us to explore a little more. We'll set a breakpoint on `http.c:705`.

```bash
(gdb) b http.c:705
Breakpoint 1 at 0x40445e: file http.c, line 705.
(gdb) r
# ... running ...
Thread 4 "server" hit Breakpoint 1, http_handle_transaction (self=0x608cc0) at http.c:705
705             strncpy(ta.req_body_str, body, ta.req_content_len);
```

Sure enough, we land on the `strncpy()` mentioned by Valgrind.

```bash
(gdb) p ta.req_content_len
$1 = 48
(gdb) p body
$2 = 0x7fffec000bf8 "{\"username\": \"user0\", \"password\": \"thep"
(gdb) p (size_t) strlen(body)
$3 = 39
```

As we suspected earlier, the server is telling `strncpy()` to copy 48 bytes from a string that's 39 bytes long. If we examine the destination buffer (`ta.req_body_str`) before and after line 705 is executed, we can determine exactly what was written to the buffer.

```bash
# BEFORE THE WRITE
(gdb) x/48c ta.req_body_str
0x7fffec002ba0: 0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'
0x7fffec002ba8: 0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'
0x7fffec002bb0: 0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'
0x7fffec002bb8: 0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'
0x7fffec002bc0: 0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'
0x7fffec002bc8: 65 'A'        -28 '\344'        1 '\001'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'
(gdb) n
706             ta.req_body_str[n - 1] = '\0';
# AFTER THE WRITE
(gdb) x/48c ta.req_body_str
0x7fffec002ba0: 123 '{'        34 '"'         117 'u'         115 's'         101 'e'         114 'r'         110 'n'          97 'a'
0x7fffec002ba8: 109 'm'       101 'e'          34 '"'          58 ':'          32 ' '          34 '"'         117 'u'         115 's'
0x7fffec002bb0: 101 'e'       114 'r'          48 '0'          34 '"'          44 ','          32 ' '          34 '"'         112 'p'
0x7fffec002bb8: 97 'a'        115 's'         115 's'         119 'w'         111 'o'         114 'r'         100 'd'          34 '"'
0x7fffec002bc0: 58 ':'         32 ' '          34 '"'         116 't'         104 'h'         101 'e'         112 'p'           0 '\000'
0x7fffec002bc8: 0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'        0 '\000'
```

Everything expected is copied over, but `strncpy()` likely attempted to copy past the available 39 bytes, causing Valgrind to flag the error. A few lines later we run into the `malloc.c` assertion error:

```bash
(gdb) n
709         validate_cookie(&ta);
(gdb) n
711         buffer_init(&ta.resp_headers, 1024);
(gdb) n
server: malloc.c:2396: sysmalloc: Assertion `(old_top == initial_top (av) && old_size == 0) || ((unsigned long) (old_size) >= MINSIZE && prev_inuse (old_top) && ((unsigned long) old_end & (pagesize - 1)) == 0)' failed.

Thread 4 "server" received signal SIGABRT, Aborted.
0x00007ffff6b1fa4f in raise () from /lib64/libc.so.6
```

## Conclusions

To check if this is the true problem, I modified the source code to be mindful of this issue by choosing the minimum value: the given `Content-Length` or the detected string length of the source buffer. Running it with this fix eliminated the `malloc.c` assertion and the `Invalid write of size 1` error from Valgrind.

In conclusion, this was certainly an interesting bug, but it was something the programmer could have fixed if they had tested thoroughly with Valgrind. Of course, the programmer might not have thought to try an incomplete JSON payload with a false `Content-Length`. That's why we have fuzzers to find bugs for us.


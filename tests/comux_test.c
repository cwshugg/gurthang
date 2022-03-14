// Tests my comux protocol/file format.
//
//      Connor Shugg

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "test.h"
#include "../src/comux/comux.h"

// Tests the comux_header_t file input/output.
static void test_header_io()
{
    test_section("comux header write");
    comux_header_t h;
    comux_header_init(&h);
    h.version = 0x11223344;
    h.num_conns = 0x55667788;
    h.num_chunks= 0x99aabbcc;

    // open a dummy file
    int fd = open("./comux_test1.txt", O_CREAT | O_WRONLY, 0644);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    // attempt to write out the header
    size_t bw = comux_header_write(&h, fd);
    check(bw == 20, "comux_header_write returned %d, not 20", bw);
    // close the file
    close(fd);

    test_section("comux header read");
    // create a new header, open the file for reading
    comux_header_t h2;
    fd = open("./comux_test1.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    // attempt to parse the file
    comux_parse_result_t result = comux_header_read(&h2, fd);
    check(result == COMUX_PARSE_OK, "comux_header_read returned %d instead of %d", result, COMUX_PARSE_OK);
    check(!strncmp(h2.magic, COMUX_MAGIC, COMUX_MAGIC_LEN), "comux_header_read filled a bag magic: %s", h2.magic);
    check(h2.version == 0x11223344, "comux_header_read gave a bad version: 0x%x", h2.version);
    check(h2.num_conns == 0x55667788, "comux_header_read gave a bad num_conns: 0x%x", h2.num_conns);
    check(h2.num_chunks == 0x99aabbcc, "comux_header_read gave a bad num_chunks: 0x%x", h2.num_chunks);
    // close the file
    close(fd);
    
    test_section("comux header buffer read-write");
    // write to a buffer
    char buff[512];
    ssize_t wcount = comux_header_write_buffer(&h, buff, 512);
    check(wcount == 20, "comux_header_write_buffer returned %ld, not 20", wcount);
    wcount = comux_header_write_buffer(&h, buff, 10);
    check(wcount == -20, "comux_header_write_buffer returned %ld, not -20", wcount);

    // read from a buffer
    comux_header_init(&h2);
    size_t rcount = 0;
    result = comux_header_read_buffer(&h2, buff, 512, &rcount);
    check(result == COMUX_PARSE_OK, "comux_header_read_buffer didn't return PARSE_OK: %s",
          comux_parse_result_string(result));
    check(rcount == 20, "comux_header_read_buffer returned size: %ld, not 20", rcount);
    check(h2.version == h.version, "buffer-read version mismatch");
    check(h2.num_conns == h.num_conns, "buffer-read num_conns mismatch");
    check(h2.num_chunks == h.num_chunks, "buffer-read num_chunks mismatch");
    // check for bad parses
    result = comux_header_read_buffer(&h2, buff, 5, &rcount);
    check(result == COMUX_PARSE_BAD_MAGIC, "comux_header_read_buffer didn't return BAD_MAGIC: %s",
          comux_parse_result_string(result));
    result = comux_header_read_buffer(&h2, buff, 10, &rcount);
    check(result == COMUX_PARSE_BAD_VERSION, "comux_header_read_buffer didn't return BAD_VERSION: %s",
          comux_parse_result_string(result));
    result = comux_header_read_buffer(&h2, buff, 14, &rcount);
    check(result == COMUX_PARSE_BAD_NUM_CONNS, "comux_header_read_buffer didn't return BAD_NUM_CONNS: %s",
          comux_parse_result_string(result));
    result = comux_header_read_buffer(&h2, buff, 18, &rcount);
    check(result == COMUX_PARSE_BAD_NUM_CHUNKS, "comux_header_read_buffer didn't return BAD_NUM_CHUNKS: %s",
          comux_parse_result_string(result));

    test_section("comux header BAD reads");

    // TEST 1: bad magic 1
    comux_header_t h3;
    fd = open("./tests/files/comux_test/comux_bad_magic1.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    check(comux_header_read(&h3, fd) == COMUX_PARSE_BAD_MAGIC,
          "comux_header_read returned %d instead of %d", result, COMUX_PARSE_BAD_MAGIC);
    close(fd);
    
    // TEST 2: bad magic 2
    fd = open("./tests/files/comux_test/comux_bad_magic2.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    check(comux_header_read(&h3, fd) == COMUX_PARSE_BAD_MAGIC,
          "comux_header_read returned %d instead of %d", result, COMUX_PARSE_BAD_MAGIC);
    close(fd);

    // TEST 3: bad version 1
    fd = open("./tests/files/comux_test/comux_bad_version1.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    check(comux_header_read(&h3, fd) == COMUX_PARSE_BAD_VERSION,
          "comux_header_read returned %d instead of %d", result, COMUX_PARSE_BAD_VERSION);
    close(fd);
}

// Tests the chunk header input/output
static void test_cinfo_io()
{
    comux_cinfo_t ci;
    comux_cinfo_init(&ci);
    ci.id = 0x11223344;
    ci.sched = 0xddee00ff;
    ci.flags = 0x87654321;

    test_section("comux cinfo write");
    // open a file and attempt to write
    int fd = open("./comux_test2.txt", O_CREAT | O_WRONLY, 0644);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    size_t bw = comux_cinfo_write(&ci, fd);
    check(bw == 20, "comux_cinfo_write returned %d, not 20", bw);
    close(fd);
    
    test_section("comux cinfo read");
    // open the file again and attempt to read
    comux_cinfo_t ci2;
    fd = open("./comux_test2.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    comux_parse_result_t result = comux_cinfo_read(&ci2, fd);
    close(fd);
    check(result == COMUX_PARSE_OK, "comux_cinfo_read returned %d instead of %d", result, COMUX_PARSE_OK);
    check(ci2.id == 0x11223344, "comux_cinfo_read gave a bad id: 0x%x", ci2.id);
    check(ci2.sched == 0xddee00ff, "comux_cinfo_read gave a bad sched: 0x%x", ci2.sched);
    check(ci2.flags == 0x87654321, "comux_cinfo_read gave bad flags: 0x%x", ci2.flags);

    test_section("comux cinfo buffer read-write");
    // write out to a buffer
    char buff[512];
    memset(buff, 0, 512);
    ssize_t wcount = comux_cinfo_write_buffer(&ci, buff, 512);
    check(wcount == 20, "comux_cinfo_write_buffer returned %ld, not 20", wcount);
    wcount = comux_cinfo_write_buffer(&ci, buff, 12);
    check(wcount == -20, "comux_cinfo_write_buffer returned %ld, not -20", wcount);

    // read from a buffer
    size_t rcount = 0;
    result = comux_cinfo_read_buffer(&ci2, buff, 512, &rcount);
    check(result == COMUX_PARSE_OK, "comux_cinfo_read_buffer didn't return PARSE_OK: %s",
          comux_parse_result_string(result));
    check(rcount == 20, "comux_cinfo_read_buffer wrong size: %lu, not 20", rcount);
    check(ci2.id == ci.id, "comux_cinfo_read_buffer id mismatch: %u vs %u", ci2.id, ci.id);
    check(ci2.len == ci.len, "comux_cinfo_read_buffer len mismatch");
    check(ci2.sched == ci.sched, "comux_cinfo_read_buffer sched mismatch");
    check(ci2.flags == ci.flags, "comux_cinfo_read_buffer flags mismatch");
    result = comux_cinfo_read_buffer(&ci2, buff, 2, &rcount);
    check(result == COMUX_PARSE_BAD_CONN_ID, "comux_cinfo_read_buffer didn't return BAD_CONN_ID: %s",
          comux_parse_result_string(result));
    result = comux_cinfo_read_buffer(&ci2, buff, 6, &rcount);
    check(result == COMUX_PARSE_BAD_CONN_LEN, "comux_cinfo_read_buffer didn't return BAD_CONN_LEN: %s",
          comux_parse_result_string(result));
    result = comux_cinfo_read_buffer(&ci2, buff, 14, &rcount);
    check(result == COMUX_PARSE_BAD_CONN_SCHED, "comux_cinfo_read_buffer didn't return BAD_CONN_SCHED: %s",
          comux_parse_result_string(result));
    result = comux_cinfo_read_buffer(&ci2, buff, 18, &rcount);
    check(result == COMUX_PARSE_BAD_CONN_FLAGS, "comux_cinfo_read_buffer didn't return BAD_CONN_FLAGS: %s",
          comux_parse_result_string(result));

    test_section("comux cinfo BAD reads");

    // TEST 1: bad id 1
    comux_cinfo_t ci3;
    comux_cinfo_init(&ci3);
    fd = open("./tests/files/comux_test/comux_bad_cinfo_id1.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    check(comux_cinfo_read(&ci3, fd) == COMUX_PARSE_BAD_CONN_ID,
          "comux_cinfo_read returned %d instead of %d", result, COMUX_PARSE_BAD_CONN_ID);
    close(fd);
    comux_cinfo_free(&ci3);

    // TEST 1: bad len 1
    comux_cinfo_init(&ci3);
    fd = open("./tests/files/comux_test/comux_bad_cinfo_len1.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    check(comux_cinfo_read(&ci3, fd) == COMUX_PARSE_BAD_CONN_LEN,
          "comux_cinfo_read returned %d instead of %d", result, COMUX_PARSE_BAD_CONN_LEN);
    close(fd);
    comux_cinfo_free(&ci3);

    // TEST 1: bad sched 1
    comux_cinfo_init(&ci3);
    fd = open("./tests/files/comux_test/comux_bad_cinfo_sched1.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    check(comux_cinfo_read(&ci3, fd) == COMUX_PARSE_BAD_CONN_SCHED,
          "comux_cinfo_read returned %d instead of %d", result, COMUX_PARSE_BAD_CONN_SCHED);
    close(fd);
    comux_cinfo_free(&ci3);

    // TEST 1: bad flags 1
    comux_cinfo_init(&ci3);
    fd = open("./tests/files/comux_test/comux_bad_cinfo_flags1.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor (%s)", strerror(errno));
    check(comux_cinfo_read(&ci3, fd) == COMUX_PARSE_BAD_CONN_FLAGS,
          "comux_cinfo_read returned %d instead of %d", result, COMUX_PARSE_BAD_CONN_FLAGS);
    close(fd);
    comux_cinfo_free(&ci3);
}

// Tests reading and writing cinfo structs with data.
static void test_cinfo_data_io()
{
    test_section("cinfo data append");
    comux_cinfo_t c;
    comux_cinfo_init(&c);
    c.id = 23;
    c.sched = 10;

    // before appending data, try to write to STDOUT. Since the buffer is empty
    // nothing should happen
    check(comux_cinfo_data_write(&c, STDOUT_FILENO) == 0, "stuff was written to stdout");

    // append some data
    comux_cinfo_data_appendf(&c, "integer: %d", 5);
    comux_cinfo_data_append(&c, " hello");
    check(c.len == 16, "data appending didn't set the length correctly: %d", c.len);
    check(c.data.size == 16, "data appending didn't set the buff size correctly: %d", c.data.size);
    check(!strcmp(buffer_dptr(&c.data), "integer: 5 hello"), "buffer has wrong value: '%s'",
          buffer_dptr(&c.data));
    
    test_section("cinfo data buffer read-write");
    // write to a buffer
    char buff1[512];
    memset(buff1, 0, 512);
    ssize_t wcount1 = comux_cinfo_data_write_buffer(&c, buff1, 512);
    check(wcount1 == 16, "comux_cinfo_data_write_buffer returned %ld, not 16", wcount1);
    check(!strcmp(buff1, "integer: 5 hello"), "incorrect buffer data");
    wcount1 = comux_cinfo_data_write_buffer(&c, buff1, 10);
    check(wcount1 == -16, "comux_cinfo_data_write_buffer returned %ld, not -16", wcount1);

    // then, read from a buffer
    comux_cinfo_t c1;
    comux_cinfo_init(&c1);
    c1.len = 16;
    size_t rcount1 = comux_cinfo_data_read_buffer(&c1, buff1, 512);
    check(rcount1 == 16, "comux_cinfo_data_read_buffer returned %lu, not 16", rcount1);
    check(!strcmp(buffer_dptr(&c1.data), "integer: 5 hello"), "incorrect buffer data");

    comux_cinfo_free(&c1);
    comux_cinfo_init(&c1);
    c1.len = 16;
    rcount1 = comux_cinfo_data_read_buffer(&c1, buff1, 10);
    check(rcount1 == 10, "comux_cinfo_data_read_buffer returned %lu, not 10", rcount1);
    check(!strcmp(buffer_dptr(&c1.data), "integer: 5"), "incorrect buffer data");
    comux_cinfo_free(&c1);
    
    test_section("cinfo data write 1");
    // write the data to a file
    int fd = open("./comux_test3.txt", O_CREAT | O_WRONLY, 0644);
    check(fd != -1, "failed to open a file descriptor");
    check(comux_cinfo_write(&c, fd) == 20, "writing the cinfo header failed");
    size_t wcount = comux_cinfo_data_write(&c, fd);
    check(wcount == 16, "comux_cinfo_data_write returned %ld, not 16", wcount);
    check(c.offset == 0, "offset is incorrect");
    close(fd);

    test_section("cinfo data read 1");
    comux_cinfo_t c2;
    comux_cinfo_init(&c2);
    // open a file and attempt to read
    fd = open("./comux_test3.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor");
    check(comux_cinfo_read(&c2, fd) == COMUX_PARSE_OK, "comux_cinfo_read failed");
    size_t rcount = comux_cinfo_data_read(&c2, fd);
    check(rcount == 16, "comux_cinfo_data_read returned %ld, not 16", rcount);
    check(c2.offset == 0, "offset is incorrect");
    close(fd);
    // compare the read-in data and other values, then free memory
    check(c2.id == 23, "c2's id is wrong");
    check(c2.sched == 10, "c2's sched is wrong");
    check(c2.flags == COMUX_CHUNK_FLAGS_NONE, "c2's flags are wrong");
    check(c2.len == 16, "c2's len is wrong: %ld", c2.len);
    check(!strcmp(buffer_dptr(&c2.data), "integer: 5 hello"), "buffer has wrong value: '%s'",
          buffer_dptr(&c2.data));
    comux_cinfo_free(&c2);

    test_section("cinfo data write 2");
    // wipe the original
    comux_cinfo_free(&c);
    comux_cinfo_init(&c);
    c.id = 12;
    c.sched = 6;
    c.flags = 0xabcd;
    // create a buffer to fill with random bytes
    size_t buff_size = 15000;
    char buff[buff_size];
    fd = open("/dev/urandom", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor");
    rcount = read(fd, buff, buff_size);
    check(rcount == buff_size, "couldn't read %ld bytes from /dev/urandom", buff_size);
    close(fd);

    // write these bytes to the cinfo's data buffer, then write it out
    comux_cinfo_data_appendn(&c, buff, buff_size);
    check(c.len == 15000, "data appending didn't set the length correctly: %d", c.len);
    check(c.data.size == 15000, "data appending didn't set the buff size correctly: %d", c.data.size);
    // open a file and write
    fd = open("comux_test4.txt", O_CREAT | O_WRONLY, 0644);
    check(fd != -1, "failed to open a file descriptor");
    check(comux_cinfo_write(&c, fd) == 20, "comux_cinfo_write failed");
    wcount = comux_cinfo_data_write(&c, fd);
    check(wcount == 15000, "comux_cinfo_data_write returned %ld, not 15000", wcount);
    check(c.offset == 0, "offset is incorrect");
    close(fd);

    test_section("cinfo data read 2");
    comux_cinfo_init(&c2);
    // open a file and attempt to read
    fd = open("./comux_test4.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor");
    check(comux_cinfo_read(&c2, fd) == COMUX_PARSE_OK, "comux_cinfo_read failed");
    rcount = comux_cinfo_data_read(&c2, fd);
    check(rcount == 15000, "comux_cinfo_data_read returned %ld, not 15000", rcount);
    check(c.offset == 0, "offset is incorrect");
    close(fd);
    // compare the read-in data and other values, then free memory
    check(c2.id == 12, "c2's id is wrong");
    check(c2.sched == 6, "c2's sched is wrong");
    check(c2.flags == 0xabcd, "c2's flags are wrong");
    check(c2.len == 15000, "c2's len is wrong");
    check(!memcmp(buffer_dptr(&c2.data), buff, buff_size), "buffer bytes don't match");
    
    comux_cinfo_free(&c2);
    comux_cinfo_free(&c);
}

// Tests the comux manifest's cinfo list
static void test_manifest_cinfo()
{
    test_section("manifest cinfo list");

    comux_manifest_t m;
    comux_manifest_init(&m);
    // make sure the list is initially empty
    check(m.cinfo_list.size == 0, "comux manifest list isn't initially empty");

    comux_cinfo_t c1;
    comux_cinfo_init(&c1);
    c1.id = 0;
    c1.sched = 5;
    comux_manifest_cinfo_add(&m, &c1);

    // check and iterate
    check(m.cinfo_list.size == 1, "comux manifest list size isn't 1");
    check(m.header.num_chunks == 1, "comux manifest num_chunks isn't 1");
    dllist_elem_t* e;
    int count = 0;
    dllist_iterate(&m.cinfo_list, e)
    {
        comux_cinfo_t* c = e->container;
        check(c->id == 0, "comux manifest list value is wrong");
        check(c->sched == 5, "comux manifest list value is wrong");
        count++;
    }
    check(count == 1, "didn't iterate once");

    comux_cinfo_t c2;
    comux_cinfo_init(&c2);
    c2.id = 1;
    c2.sched = 6;
    comux_manifest_cinfo_add(&m, &c2);

    // check and iterate
    check(m.cinfo_list.size == 2, "comux manifest list size isn't 2");
    check(m.header.num_chunks == 2, "comux manifest num_chunks isn't 2");
    count = 0;
    dllist_iterate(&m.cinfo_list, e)
    {
        comux_cinfo_t* c = e->container;
        if (count == 0)
        {
            check(c->id == 0, "comux manifest list value 1 is wrong");
            check(c->sched == 5, "comux manifest list value 1 is wrong");
        }
        else if (count == 1)
        {
            check(c->id == 1, "comux manifest list value 2 is wrong");
            check(c->sched == 6, "comux manifest list value 2 is wrong");
        }
        count++;
    }
    check(count == 2, "didn't iterate twice");

    // remove an entry, then iterate and check
    comux_manifest_cinfo_remove(&m, c1.id);
    check(m.cinfo_list.size == 1, "comux manifest list size isn't 1");
    check(m.header.num_chunks == 1, "comux manifest num_chunks isn't 1");
    count = 0;
    dllist_iterate(&m.cinfo_list, e)
    {
        comux_cinfo_t* c = e->container;
        check(c->id == 1, "comux manifest list value is wrong");
        check(c->sched == 6, "comux manifest list value is wrong");
        count++;
    }
    check(count == 1, "didn't iterate once");
}

// Full test of writing & reading an entire comux file
static void test_manifest_full_io()
{
    test_section("manifest full write");
    
    comux_manifest_t m;
    comux_manifest_init(&m);
    size_t expected_bytes = 20;

    // add one chunk to the file
    comux_cinfo_t c1;
    comux_cinfo_init(&c1);
    c1.id = 0;
    c1.sched = 0;
    c1.flags = COMUX_CHUNK_FLAGS_NONE;
    comux_cinfo_data_appendf(&c1, "conn1's data: %d", 23);
    comux_manifest_cinfo_add(&m, &c1);
    expected_bytes += 20 + 16;
    // make some checks
    check(m.cinfo_list.size == 1, "list didn't get updated");
    check(m.header.num_chunks == 1, "num_chunks didn't get updated");

    // add another chunk to the file
    comux_cinfo_t c2;
    comux_cinfo_init(&c2);
    c2.id = 1;
    c2.sched = 1;
    c2.flags = COMUX_CHUNK_FLAGS_NONE;
    comux_cinfo_data_appendf(&c2, "chunk 2 gets more data than chunk 1");
    comux_manifest_cinfo_add(&m, &c2);
    expected_bytes += 20 + 35;
    // make some checks
    check(m.cinfo_list.size == 2, "list didn't get updated");
    check(m.header.num_chunks == 2, "num_chunks didn't get updated");

    // open a file and try to write out
    int fd = open("comux_test5.txt", O_CREAT | O_WRONLY, 0644);
    check(fd != -1, "failed to open a file descriptor");
    size_t wcount = comux_manifest_write(&m, fd);
    check(wcount == expected_bytes, "comux_manifest_write returned %ld, not %ld",
          wcount, expected_bytes);
    check(c1.offset == 20, "c1's offset is %ld", c1.offset);
    check(c2.offset == 56, "c2's offset is %ld", c2.offset);
    close(fd);

    test_section("manifest buffer full read-write");
    // write out to a buffer
    char buff[512];
    ssize_t wcount1 = comux_manifest_write_buffer(&m, buff, 512);
    check(wcount1 == expected_bytes, "comux_manifest_write_buffer returned %ld, not %ld",
          wcount1, expected_bytes);
    wcount1 = comux_manifest_write_buffer(&m, buff, 100);
    check(wcount1 < 0, "comux_manifest_write_buffer didn't return a negative value");

    // read from a buffer
    comux_manifest_t m0;
    comux_manifest_init(&m0);
    size_t rcount = 0;
    comux_parse_result_t result = comux_manifest_read_buffer(&m0, buff, 512, &rcount);
    check(result == COMUX_PARSE_OK, "comux_manifest_read_buffer didn't return PARSE_OK: %s",
          comux_parse_result_string(result));
    check(rcount == expected_bytes, "comux_manifest_read_buffer size mismatch: %ld vs %ld", rcount, expected_bytes);
    check(m0.header.version == m.header.version, "comux_manifest_read_buffer version mismatch");
    check(m0.header.num_conns == m.header.num_conns, "comux_manifest_read_buffer num_conns mismatch");
    check(m0.header.num_chunks == m.header.num_chunks, "comux_manifest_read_buffer num_chunks mismatch");
    // check cinfos
    dllist_elem_t* e;
    int count = 0;
    dllist_iterate(&m0.cinfo_list, e)
    {
        comux_cinfo_t* cinfo = e->container;

        if (count == 0)
        {
            check(cinfo->id == 0, "entry 1 id mismatch");
            check(cinfo->len == 16, "entry 1 len mismatch");
            check(cinfo->sched == 0, "entry 1 sched mismatch");
            check(cinfo->flags == COMUX_CHUNK_FLAGS_NONE, "entry 1 flags mismatch");
            check(buffer_size(&cinfo->data) == 16, "entry 1 data size mismatch");
            check(!strcmp(buffer_dptr(&cinfo->data), "conn1's data: 23"),
                  "entry 1 data content mismatch");
        }
        else if (count == 1)
        {
            check(cinfo->id == 1, "entry 2 id mismatch");
            check(cinfo->len == 35, "entry 2 len mismatch");
            check(cinfo->sched == 1, "entry 2 sched mismatch");
            check(cinfo->flags == COMUX_CHUNK_FLAGS_NONE, "entry 2 flags mismatch");
            check(buffer_size(&cinfo->data) == 35, "entry 2 data size mismatch");
            check(!strcmp(buffer_dptr(&cinfo->data), "chunk 2 gets more data than chunk 1"),
                  "entry 2 data content mismatch");
        }
        count++;
    }
    comux_manifest_free(&m0, 1);

    test_section("manifest full read");

    comux_manifest_t m1;
    comux_manifest_init(&m1);

    // open the file and attempt to read it
    fd = open("comux_test5.txt", O_RDONLY);
    check(fd != -1, "failed to open a file descriptor");
    comux_parse_result_t res = comux_manifest_read(&m1, fd);
    check(res == COMUX_PARSE_OK, "comux_manifest_read returned %d, not %d",
          res, COMUX_PARSE_OK);
    close(fd);

    // make comparisons between the two structs
    check(m.header.num_conns == m1.header.num_conns, "num_conns mismatch (%d vs %d)",
          m.header.num_conns, m1.header.num_conns);
    check(m.header.version == m1.header.version, "version mismatch");
    check(m.cinfo_list.size == m1.cinfo_list.size, "list size mismatch");
    // iterate through the list
    count = 0;
    dllist_iterate(&m1.cinfo_list, e)
    {
        comux_cinfo_t* cinfo = e->container;

        if (count == 0)
        {
            check(cinfo->id == 0, "entry 1 id mismatch");
            check(cinfo->len == 16, "entry 1 len mismatch");
            check(cinfo->sched == 0, "entry 1 sched mismatch");
            check(cinfo->flags == COMUX_CHUNK_FLAGS_NONE, "entry 1 flags mismatch");
            check(cinfo->offset == 20, "entry 1 offset mismatch");
            check(buffer_size(&cinfo->data) == 16, "entry 1 data size mismatch");
            check(!strcmp(buffer_dptr(&cinfo->data), "conn1's data: 23"),
                  "entry 1 data content mismatch");
        }
        else if (count == 1)
        {
            check(cinfo->id == 1, "entry 2 id mismatch");
            check(cinfo->len == 35, "entry 2 len mismatch");
            check(cinfo->sched == 1, "entry 2 sched mismatch");
            check(cinfo->flags == COMUX_CHUNK_FLAGS_NONE, "entry 2 flags mismatch");
            check(cinfo->offset == 56, "entry 2 offset mismatch");
            check(buffer_size(&cinfo->data) == 35, "entry 2 data size mismatch");
            check(!strcmp(buffer_dptr(&cinfo->data), "chunk 2 gets more data than chunk 1"),
                  "entry 2 data content mismatch");
        }
        count++;
    }

    // free all memory
    comux_manifest_free(&m, 0);
    comux_manifest_free(&m1, 1);
}

int main()
{
    test_section("comux init");

    // initialize a default cinfo struct
    comux_cinfo_t c1;
    comux_cinfo_init(&c1);
    check(c1.id == 0, "comux_cinfo_init didn't set 'cid' correctly");
    check(c1.data.size == 0, "comux_cinfo_init didn't set 'data.size' correctly");
    check(c1.sched == 0, "comux_cinfo_init didn't set 'sched' correctly");
    check(c1.flags == COMUX_CHUNK_FLAGS_NONE, "comux_cinfo_init didn't set 'flags' correctly");

    comux_header_t h1;
    comux_header_init(&h1);
    check(!strcmp(h1.magic, COMUX_MAGIC), "comux_header_init didn't set 'magic' correctly");
    check(h1.num_conns == 0, "comux_header_init didn't set 'num_conns' correctly");
    check(h1.version == 0, "comux_header_init didn't set 'version' correctly");

    comux_manifest_t m1;
    comux_manifest_init(&m1);
    check(!strcmp(m1.header.magic, COMUX_MAGIC), "comux_manifest_init didn't set 'header' correctly");
    check(m1.header.num_conns == 0, "comux_manifest_init didn't set 'header' correctly");
    check(m1.header.version == 0, "comux_manifest_init didn't set 'header' correctly");
    check(m1.cinfo_list.size == 0, "comux_manifest_init didn't set 'cinfo_list' correctly");

    // run other tests
    test_header_io();
    test_cinfo_io();
    test_cinfo_data_io();
    test_manifest_cinfo();
    test_manifest_full_io();

    test_finish();
}

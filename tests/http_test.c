// Test module for http.c/http.h.
//
//      Connor Shugg

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "../src/http/http.h"
#include "../src/utils/utils.h"

// Function used to test http headers.
static void test_headers()
{
    test_section("header basics");
    http_header_t header;
    // initialize and check defaults
    http_header_init(&header);
    check(buffer_dptr(&header.name) == NULL && buffer_size(&header.name) == 0, "header name defaults");
    check(buffer_dptr(&header.value) == NULL && buffer_size(&header.value) == 0, "header value defaults");

    // set the name
    size_t res = http_header_set_name(&header, "Content-Length");
    check(res == 14, "header name set return value: %d", res);
    check(!strcmp(buffer_dptr(&header.name), "Content-Length"), "header name is: %s",
          buffer_dptr(&header.name));
    check(buffer_size(&header.name) == 14, "header name length is %ld, not %ld",
          buffer_size(&header.name), 14);
    
    // set the value
    res = http_header_set_value(&header, "%d", 123);
    check(res == 3, "header value set return value");
    check(!strcmp(buffer_dptr(&header.value), "123"), "header value is: %s",
          buffer_dptr(&header.value));
    check(buffer_size(&header.value) == 3, "header value length if %ld, not %ld",
          buffer_size(&header.value), 3);
    
    // free the header object's memory, and the pointer itself
    http_header_free(&header);

    // set a new name - one that's much longer
    size_t too_long = 2048;
    char name[too_long];
    memset(name, 0x41, too_long);
    name[too_long] = '\0';
    res = http_header_set_name(&header, "%s", name);
    check(res == 2048, "header long name set return value: %d", res);
    check(buffer_size(&header.name) == 2048,
          "header value length is %ld, not %ld",
          buffer_size(&header.name), 2048);
    
    // free memory
    http_header_free(&header);
}

// Used to test the parsing of raw strings into headers
static void test_header_parsing()
{
    http_header_t header;
    http_header_init(&header);
    
    test_section("header invalid parsing");
    // INVALID CHECKS
    char* raw1 = "no-colon in-this-string\r\n";
    check(http_header_parse(&header, raw1) == HTTP_PARSE_INVALID_HEADER,
          "invalid header test 1");
    
    char* raw2 = ": no header name\r\n";
    check(http_header_parse(&header, raw2) == HTTP_PARSE_INVALID_HEADER,
          "invalid header test 2");
    
    char* raw3 = "  \t : no header name, again\r\n";
    check(http_header_parse(&header, raw3) == HTTP_PARSE_INVALID_HEADER,
          "invalid header test 3");
    
    char* raw4 = "no-header-value:\r\n";
    check(http_header_parse(&header, raw4) == HTTP_PARSE_INVALID_HEADER,
          "invalid header test 4");
    
    char* raw5 = "no-header-value:\t   \t\r\n";
    check(http_header_parse(&header, raw5) == HTTP_PARSE_INVALID_HEADER,
          "invalid header test 5");
    
    char* raw6 = "HeaderName WithWhitespace : valid-value\r\n";
    check(http_header_parse(&header, raw6) == HTTP_PARSE_INVALID_HEADER,
          "invalid header test 6");
    
    char* raw6_1 = "Valid-Name: Valid-Value\r";
    check(http_header_parse(&header, raw6_1) == HTTP_PARSE_INVALID_LINE_ENDING,
          "invalid header test 6_1");
    
    char* raw6_2 = "Valid-Name: Valid-Value\n";
    check(http_header_parse(&header, raw6_2) == HTTP_PARSE_INVALID_LINE_ENDING,
          "invalid header test 6_2");
    
    char* raw6_3 = "Valid-Name: Valid-Value";
    check(http_header_parse(&header, raw6_3) == HTTP_PARSE_INVALID_LINE_ENDING,
          "invalid header test 6_3");
    
    test_section("header parsing valid");
    // VALID CHECKS
    char* raw7 = "Valid-Name: Valid-Value\r\n";
    check(http_header_parse(&header, raw7) == HTTP_PARSE_OK,
          "valid header test 1");
    check(!strcmp(buffer_dptr(&header.name), "Valid-Name"), "valid header test 1: %s", buffer_dptr(&header.name));
    check(!strcmp(buffer_dptr(&header.value), "Valid-Value"), "valid header test 1: %s", buffer_dptr(&header.value));
    http_header_free(&header);
    
    char* raw8 = "\tValid-Name  :   Valid-Value  \r\n";
    check(http_header_parse(&header, raw8) == HTTP_PARSE_OK,
          "valid header test 1");
    check(!strcmp(buffer_dptr(&header.name), "Valid-Name"), "valid header test 1: %s", buffer_dptr(&header.name));
    check(!strcmp(buffer_dptr(&header.value), "Valid-Value  "), "valid header test 1: %s", buffer_dptr(&header.value));
    http_header_free(&header);
    
    char* raw9 = "Valid-Name:Valid-Value\r\n";
    check(http_header_parse(&header, raw9) == HTTP_PARSE_OK,
          "valid header test 1");
    check(!strcmp(buffer_dptr(&header.name), "Valid-Name"), "valid header test 1: %s", buffer_dptr(&header.name));
    check(!strcmp(buffer_dptr(&header.value), "Valid-Value"), "valid header test 1: %s", buffer_dptr(&header.value));
    http_header_free(&header);
    
    char* raw10 = "Valid-Name :Valid-Value\r\n";
    check(http_header_parse(&header, raw10) == HTTP_PARSE_OK,
          "valid header test 1");
    check(!strcmp(buffer_dptr(&header.name), "Valid-Name"), "valid header test 1: %s", buffer_dptr(&header.name));
    check(!strcmp(buffer_dptr(&header.value), "Valid-Value"), "valid header test 1: %s", buffer_dptr(&header.value));
    http_header_free(&header);
}

// Tests HTTP method parsing to and from strings.
static void test_method_parsing()
{
    test_section("method from string");

    // test parsing valid methods
    http_method_t m = http_method_from_string("GET");
    check(m == HTTP_METHOD_GET, "http_method_from_string didn't return GET");
    m = http_method_from_string("POST");
    check(m == HTTP_METHOD_POST, "http_method_from_string didn't return POST");
    m = http_method_from_string("PUT");
    check(m == HTTP_METHOD_PUT, "http_method_from_string didn't return PUT");
    m = http_method_from_string("CONNECT");
    check(m == HTTP_METHOD_CONNECT, "http_method_from_string didn't return CONNECT (%d)", m);
    m = http_method_from_string("DELETE");
    check(m == HTTP_METHOD_DELETE, "http_method_from_string didn't return DELETE");
    m = http_method_from_string("HEAD");
    check(m == HTTP_METHOD_HEAD, "http_method_from_string didn't return HEAD");
    m = http_method_from_string("OPTIONS");
    check(m == HTTP_METHOD_OPTIONS, "http_method_from_string didn't return OPTIONS");
    m = http_method_from_string("PATCH");
    check(m == HTTP_METHOD_PATCH, "http_method_from_string didn't return PATCH");
    m = http_method_from_string("TRACE");
    check(m == HTTP_METHOD_TRACE, "http_method_from_string didn't return TRACE");

    // test paring invalid methods
    m = http_method_from_string("NOPE");
    check(m == HTTP_METHOD_UNKNOWN, "http_method_from_string didn't return UNKNOWN for 'NOPE'");
    m = http_method_from_string("get");
    check(m == HTTP_METHOD_UNKNOWN, "http_method_from_string didn't return UNKNOWN for 'get'");

    test_section("method to string");

    // test getting strings from a valid method enum
    char buff[16];
    check(http_method_to_string(HTTP_METHOD_CONNECT, buff) == 7, "http_method_to_string(CONNECT) failed");
    check(!strcmp(buff, "CONNECT"), "http_method_to_string(CONNECT) filled the buffer with: '%s'", buff);
    check(http_method_to_string(HTTP_METHOD_DELETE, buff) == 6, "http_method_to_string(DELETE) failed");
    check(!strcmp(buff, "DELETE"), "http_method_to_string(DELETE) filled the buffer with: '%s'", buff);
    check(http_method_to_string(HTTP_METHOD_GET, buff) == 3, "http_method_to_string(GET) failed");
    check(!strcmp(buff, "GET"), "http_method_to_string(GET) filled the buffer with: '%s'", buff);
    check(http_method_to_string(HTTP_METHOD_HEAD, buff) == 4, "http_method_to_string(HEAD) failed");
    check(!strcmp(buff, "HEAD"), "http_method_to_string(HEAD) filled the buffer with: '%s'", buff);
    check(http_method_to_string(HTTP_METHOD_OPTIONS, buff) == 7, "http_method_to_string(OPTIONS) failed");
    check(!strcmp(buff, "OPTIONS"), "http_method_to_string(OPTIONS) filled the buffer with: '%s'", buff);
    check(http_method_to_string(HTTP_METHOD_PATCH, buff) == 5, "http_method_to_string(PATCH) failed");
    check(!strcmp(buff, "PATCH"), "http_method_to_string(PATCH) filled the buffer with: '%s'", buff);
    check(http_method_to_string(HTTP_METHOD_POST, buff) == 4, "http_method_to_string(POST) failed");
    check(!strcmp(buff, "POST"), "http_method_to_string(POST) filled the buffer with: '%s'", buff);
    check(http_method_to_string(HTTP_METHOD_PUT, buff) == 3, "http_method_to_string(PUT) failed");
    check(!strcmp(buff, "PUT"), "http_method_to_string(PUT) filled the buffer with: '%s'", buff);
    check(http_method_to_string(HTTP_METHOD_TRACE, buff) == 5, "http_method_to_string(TRACE) failed");
    check(!strcmp(buff, "TRACE"), "http_method_to_string(TRACE) filled the buffer with: '%s'", buff);

    // test getting strings from an invalid method
    buff[0] = '\0';
    check(http_method_to_string(HTTP_METHOD_UNKNOWN, buff) == -1, "http_method_to_string(UNKNOWN) failed");
    check(strlen(buff) == 0, "http_method_to_string(UNKNOWN) touched the buffer");
}

// Function used to test the conversion of a HTTP version to and from strings.
static void test_version_parsing()
{
    test_section("version from string");

    // test parsing valid versions
    http_version_t v = http_version_from_string("HTTP/0.9");
    check(v == HTTP_VERSION_0_9, "http_version_from_string didn't return 0.9");
    v = http_version_from_string("HTTP/1.0");
    check(v == HTTP_VERSION_1_0, "http_version_from_string didn't return 1.0");
    v = http_version_from_string("HTTP/1.1");
    check(v == HTTP_VERSION_1_1, "http_version_from_string didn't return 1.1");

    // test parsing invalid versions
    v = http_version_from_string("HTTP/7.1");
    check(v == HTTP_VERSION_UNKNOWN, "http_version_from_string didn't return UNKNOWN");

    test_section("version to string");

    // test getting strings from valid versions
    char buff[16];
    check(http_version_to_string(HTTP_VERSION_0_9, buff) == 8, "http_version_to_string(0.9) failed");
    check(!strcmp(buff, "HTTP/0.9"), "http_Version_to_string(0.9) filled the buffer with: '%s'", buff);
    check(http_version_to_string(HTTP_VERSION_1_0, buff) == 8, "http_version_to_string(1.0) failed");
    check(!strcmp(buff, "HTTP/1.0"), "http_Version_to_string(1.0) filled the buffer with: '%s'", buff);
    check(http_version_to_string(HTTP_VERSION_1_1, buff) == 8, "http_version_to_string(1.1) failed");
    check(!strcmp(buff, "HTTP/1.1"), "http_Version_to_string(1.1) filled the buffer with: '%s'", buff);

    // test getting strings from valid versions
    buff[0] = '\0';
    check(http_version_to_string(HTTP_VERSION_UNKNOWN, buff) == -1, "http_version_to_string(UNKNOWN) failed");
    check(strlen(buff) == 0, "http_version_to_string(UNKNOWN) touched the buffer");
}

// Main function
int main()
{
    test_headers();
    test_header_parsing();
    test_method_parsing();
    test_version_parsing();
    test_finish();
}


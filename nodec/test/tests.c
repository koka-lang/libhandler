#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "sbuf.h"
#include "kvp.h"
#include "request.h"
#include "http_parser.h"
#include <assert.h>

//---------------------------[ test1 ]-----------------------------------------
// testing allocating and freeing memory
//-----------------------------------------------------------------------------
void test1()
{
    void* p = debug_realloc(0, 10);
    debug_free(p);
    p = 0;
}

//---------------------------[ test2 ]-----------------------------------------
// a simple test of a strings buffer
//-----------------------------------------------------------------------------
void test2()
{
    sbuf_t sbuf = { 0, 0, 0, 0 };

    size_t const start_key = sbuf_add(&sbuf, "do", 2, 16);
    printf("len: %d\n", sbuf_get_string_length(&sbuf));
    sbuf_append(&sbuf, "g", 1, 16);
    printf("len: %d\n", sbuf_get_string_length(&sbuf));

    size_t const start_value = sbuf_add(&sbuf, "mamal", 5, 16);
    printf("len: %d\n", sbuf_get_string_length(&sbuf));

    hexDump(sbuf.buffer, sbuf.total, "\nsbuf.buffer:");

    printf("\n");
    printf("key:\"%s\"\n", sbuf_get_string(&sbuf, start_key));
    printf("value:\"%s\"\n", sbuf_get_string(&sbuf, start_value));
    printf("\n");

    sbuf_delete(&sbuf);
}

//---------------------------[ test3 ]-----------------------------------------
// testing the key-value pair architecture
//-----------------------------------------------------------------------------
void test3()
{
    struct {
        const char* key;
        const char* value;
    } kvps[] =
    {
        { "Dog", "Mamal" },
        { "Shark", "Fish" },
        { "Ant", "Insect"},
    };

    sbuf_t sbuf = { 0, 0, 0, 0 };
    kvpbuf_t kvpbuf = { 0, 0, 0 };

    for (size_t i = 0; i < _countof(kvps); i++) {
        const char* key = kvps[i].key;
        const char* value = kvps[i].value;

        kvp_t kvp = { { 0, 0 }, { 0, 0 } };
        kvp.key.length = strlen(key);
        kvp.value.length = strlen(value);

        kvp.key.start = sbuf_add(&sbuf, key, kvp.key.length, 1024);
        kvp.value.start = sbuf_add(&sbuf, value, kvp.value.length, 1024);

        kvpbuf_add(&kvpbuf, &kvp, 8);
    }

    for (size_t i = 0; i < kvpbuf.used; i++) {
        kvp_t* p_kvp = kvpbuf.buffer + i;
        const char* key = sbuf_get_string(&sbuf, p_kvp->key.start);
        const char* value = sbuf_get_string(&sbuf, p_kvp->value.start);
        printf("\n");
        printf("key:\"%s\" (%u)\n", key, p_kvp->key.length);
        printf("value:\"%s\" (%u)\n", value, p_kvp->value.length);
    }
    printf("\n");
    kvpbuf_delete(&kvpbuf);
    sbuf_delete(&sbuf);
}

//---------------------------[ header_callback_data_t ]------------------------
// used in test4
//-----------------------------------------------------------------------------
typedef struct _header_callback_data_t {
    size_t count;
} header_callback_data_t;

//---------------------------[ header_callback ]-------------------------------
// used in test4
//-----------------------------------------------------------------------------
static void header_callback(const header_t* header, void* data)
{
    header_callback_data_t* callback_data = (header_callback_data_t*)data;
    printf("%u: { \"%s\", \"%s\" }\n",
        callback_data->count, header->field.s, header->value.s);
    callback_data->count += 1;
}

//---------------------------[ test4 ]-----------------------------------------
// parse an HTTP request
//-----------------------------------------------------------------------------
void test4()
{
    static void test_request(http_request_t* req);
	http_request_t* req = http_request_alloc();
    test_request(req);
	http_request_free(req);
}


//---------------------------[ test_request ]----------------------------------
// used by test4
//-----------------------------------------------------------------------------
static void test_request(http_request_t* req)
{
    static void process_completed_request(http_request_t* req);
    const char* request_string =
        "GET /docs/index.html HTTP/1.1\r\n"
        "Host: www.nowhere123.com\r\n"
        "Accept: image / gif, image / jpeg, */*\r\n"
        "Accept-Language: en-us\r\n"
        "Accept-Encoding: gzip, deflate\r\n"
        "User-Agent: Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1)\r\n"
        "accept-Language: Eastern Canadian\r\n"
        "Content-Length: 5\r\n"
        "accept-language: High German\r\n"
        "\r\n"
        "12345";

    // break the request string into chunks
    const size_t len_request_string = strlen(request_string);
    size_t start_points[] = { 0, 4, 8, 24, 32, 48,
        50, 60, 65, 68, 70, len_request_string };
    assert(_countof(start_points) > 2);
    assert(start_points[_countof(start_points) - 2] < len_request_string);

    // execute the chunks
    for (size_t i = 0; i < _countof(start_points) - 1; i++) {
        size_t start = start_points[i];
        size_t len = start_points[i + 1] - start;
        http_request_execute(req, request_string + start, len);
    }

    // print some results
    if (http_request_headers_are_complete(req))
        process_completed_request(req);
}

//---------------------------[ process_completed_request ]---------------------
// Called by test_request
//-----------------------------------------------------------------------------
static void process_completed_request(http_request_t* req)
{
    const char* method_names[] =
    {
#define T(num, name, string) #string,
        HTTP_METHOD_MAP(T)
#undef T
    };

    printf("\n\n--------------------------------"
           "--------------------------------------------------\n");
    printf("http_major: %u\n", http_request_http_major(req));
    printf("http_minor: %u\n", http_request_http_minor(req));
    printf("content_length: %llu\n", http_request_content_length(req));
    const enum http_method method = http_request_method(req);
    printf("method: %d (%s)\n", method, method_names[method]);
    string_t url = http_request_url(req);
    if (url.s != 0 && url.len > 0)
        printf("url: \"%s\"\n", url.s);
    printf("\n");

    header_callback_data_t header_callback_data = { 0 };
    http_request_iter_headers(req, header_callback, &header_callback_data);

    extern bool header_filter(const header_t* header, void *data);
    extern void filter_headers_callback(const header_t* header, void* data);

    const char* const field = "accept-language";
    string_t filter_string = { field, strlen(field) };
    printf("\nFinding headers with fields"
        " matching \"accept-language\" ingnoring case ...\n\n");
    http_request_filter_headers(
        req,
        header_filter, &filter_string,
        filter_headers_callback, 0);
    printf("\n");
    printf("-------------------------------------------"
           "---------------------------------------\n\n");
}

//---------------------------[ string_filter ]---------------------------------
// used by process_completed_request. This filter compares the field
// values of the two headers. The string comparison is case independent.
// If the field names are the same, independent of case, this returns true.
//-----------------------------------------------------------------------------
bool header_filter(const header_t* header, void *data)
{
    const string_t* const field = (const string_t*)data;
    bool ans = false;
    if (header->field.len == field->len) {
        if (_strnicmp(header->field.s, field->s, field->len) == 0)
            ans = true;
    }
    return ans;
}

//---------------------------[ filter_headers_callback ]-----------------------
// used by process_completed_request. Prints the contents of the header,
// that is, it prints the field and value strings.
//-----------------------------------------------------------------------------
void filter_headers_callback(const header_t* header, void* data)
{
    printf("    \"%s\":\"%s\"\n", header->field.s, header->value.s);
}

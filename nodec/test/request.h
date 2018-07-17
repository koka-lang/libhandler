#pragma once
#include <stdbool.h>
#include "http_parser.h"
#include "sbuf.h"
#include "kvp.h"

/*-----------------------------------------------------------------------------
    string_t

        a safe string containing a pointer to the start of the string
        along with the length of the string not including any terminator
-----------------------------------------------------------------------------*/
typedef struct _string_t {
  const char* s;
  size_t len;
} string_t;

/*-----------------------------------------------------------------------------
    header_t

        for iterating through HTTP request headers
-----------------------------------------------------------------------------*/
typedef struct _header_t {
  string_t field;
  string_t value;
} header_t;

/*-----------------------------------------------------------------------------
    http_parser_callback

        list of all possible http_parser_callbacks
-----------------------------------------------------------------------------*/
typedef enum _http_parser_callback {
  ON_NONE = 0,
  ON_MESSAGE_BEGIN,
  ON_URL,
  ON_STATUS,
  ON_HEADER_FIELD,
  ON_HEADER_VALUE,
  ON_HEADERS_COMPLETE,
  ON_BODY,
  ON_MESSAGE_COMPLETE,
  ON_CHUNK_HEADER,
  ON_CHUNK_COMPLETE
} http_parser_callback;

/*-----------------------------------------------------------------------------
    http_request_t

        contains all the data for processing the HTTP request
        holds stat for the http_parser callback functions
-----------------------------------------------------------------------------*/
typedef struct _http_request_t {
  http_parser parser;
  http_parser_settings settings;
  bool headers_are_complete;
  uint64_t content_length;    // -1 if not set in headers
  sbuf_t sbuf;
  size_t sbuf_inc;    // charcter chunk increment
  kvp_t kvp;
  kvpbuf_t kvpbuf;
  size_t kvpbuf_inc;  // increment of the number of key-value pairs
  pascal_string_t url;
  http_parser_callback previous;
  bool connection_close;
  bool body_is_final;
} http_request_t;

/*-----------------------------------------------------------------------------
    default array sizes
-----------------------------------------------------------------------------*/
#define HTTP_REQUEST_DEFAULT_SBUF_INC 32
#define HTTP_REQUEST_DEFAULT_KVPBUF_INC 2

/*-----------------------------------------------------------------------------
    exports
-----------------------------------------------------------------------------*/
http_request_t* http_request_alloc();
void http_request_free(http_request_t* self);
size_t http_request_execute( http_request_t* self, const char* data, size_t length);
bool http_request_headers_are_complete(const http_request_t* self);
uint64_t http_request_content_length(const http_request_t* self);
unsigned short http_request_http_major(const http_request_t* self);
unsigned short http_request_http_minor(const http_request_t* self);
enum http_method http_request_method(const http_request_t* self);
string_t http_request_url(const http_request_t* self);
void http_request_iter_headers(const http_request_t* self, void(*callback)(const header_t*, void*), void* data);
void http_request_filter_headers(const http_request_t* self, bool(*filter)(const header_t*, void*), void* filter_data, void(*callback)(const header_t* header, void*), void* callback_data);

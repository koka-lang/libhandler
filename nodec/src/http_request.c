/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "nodec.h"
#include "nodec-primitive.h"
#include "nodec-internal.h"
#include <assert.h>

#define HTTP_MAX_HEADERS (8*1024)

/*-----------------------------------------------------------------
HTTP Headers
-----------------------------------------------------------------*/


typedef struct _http_header_t {
  const char* name;
  const char* value;
  bool _do_free;
} http_header_t;


typedef struct _http_headers_t {
  size_t  count;         // how many are there
  size_t  size;          // how big is our array
  http_header_t* elems;  // realloc on demand, perhaps start with size 8 and do +8 as it comes?  
} http_headers_t;

static void http_headers_add(http_headers_t* headers, const char* name, const char* value, bool strdup) {
  if (name == NULL) return;
  if (headers->count >= headers->size) {
    size_t newsize = (headers->size == 0 ? 16 : 2 * headers->size);
    headers->elems = nodec_realloc_n(headers->elems, newsize, http_header_t);
  }
  http_header_t* h = &headers->elems[headers->count];
  headers->count++;
  h->name = strdup ? nodec_strdup(name) : name;
  h->value = strdup ? nodec_strdup(value) : name;
  h->_do_free = !strdup;
}

static void http_header_clear(http_header_t* header) {
  if (header->_do_free) {
    if (header->name != NULL) nodec_free(header->name);
    if (header->value != NULL) nodec_free(header->value);
  }
  memset(header, 0, sizeof(http_header_t));
}

static void http_headers_clear(http_headers_t* headers) {
  for (size_t i = 0; i < headers->count; i++) {
    http_header_clear(&headers->elems[i]);
  }
  if (headers->elems != NULL) nodec_free(headers->elems);
  memset(headers, 0, sizeof(http_headers_t));
}


// Find a header value and normalize if necessary by appending with commas
static const char* http_headers_lookup_from(http_headers_t* headers, const char* name, size_t from) {
  http_header_t* found = NULL;
  uv_buf_t newvalue = nodec_buf_null();
  for (size_t i = from; i < headers->count; i++) {
    http_header_t* h = &headers->elems[i];
    if (h->name == NULL || h->value == NULL) continue;
    if (_stricmp(name, h->name) == 0) {
      if (found == NULL) {
        // found first entry
        found = h;
      }
      else {
        // found another entry.. we string append into the first entry and NULL this one out
        size_t n = strlen(h->value);
        // reallocate
        if (newvalue.base == NULL) {
          size_t m = strlen(found->value);
          newvalue.base = nodec_alloc_n(m + n + 2, char);
          memcpy(newvalue.base, found->value, m);
          newvalue.len = (uv_buf_len_t)m;
        }
        else {
          newvalue.base = nodec_realloc_n(newvalue.base, newvalue.len + n + 2, char);  // 2: , + 0
        }
        // append comma and the current value
        newvalue.base[newvalue.len] = ',';
        memcpy(newvalue.base + newvalue.len + 1, h->value, n);
        newvalue.len += (uv_buf_len_t)n;
        newvalue.base[newvalue.len] = 0;
        http_header_clear(h);  // clear the current entry
      }
    }
  }
  if (found == NULL) return NULL;
  if (newvalue.base != NULL) {
    // update our found entry with the new appended value
    if (found->_do_free) {
      nodec_free(found->value);
    }
    else {
      found->_do_free = true;
      found->name = nodec_strdup(found->name);
    }
    found->value = newvalue.base;
  }
  return found->value;
}

// Lookup a specific header entry (case insensitive), returning its value or NULL if not found.
static const char* http_headers_lookup(http_headers_t* headers, const char* name) {
  return http_headers_lookup_from(headers, name, 0);
}

// Iterate through all entries. `*iter` should start at 0. Returns NULL if done iterating.
static const char* http_headers_next(http_headers_t* headers, const char** value, size_t* iter) {
  if (value != NULL) *value = NULL;
  if (iter == NULL) return NULL;
  while (*iter < headers->count && headers->elems[*iter].name == NULL) {
    (*iter)++;
  }
  if (*iter >= headers->count) return NULL;
  // normalize the entry by lookup itself directly
  const char* name = headers->elems[*iter].name;
  const char* _value = http_headers_lookup_from(headers, name, *iter);
  if (value != NULL) *value = _value;
  (*iter)++;
  return name;
}

/*-----------------------------------------------------------------
Requests
-----------------------------------------------------------------*/

typedef struct _http_req_t
{
  uv_stream_t*    stream; // the input stream
  http_parser     parser; // the request parser on the stream
  http_parser_settings parser_settings;

  const char*     url;     // parsed url
  http_headers_t  headers; // parsed headers; usually pointing into `prefix`
  uv_buf_t        prefix;  // the initially read buffer that holds all initial headers

  uv_buf_t        current;        // the last read buffer; starts equal to prefix
  size_t          current_offset; // parsed up to this point into the current buffer
  uv_buf_t        current_body;   // the last parsed body piece; each on_body pauses the parser so only one is needed
  const char*     current_field;  // during header parsing, holds the last seen header field

  bool            headers_complete;  // true if all initial headers have been parsed
  bool            complete;          // true if the whole message has been parsed
} http_req_t;

// Terminate header fields and values by modifying the read buffer in place. (in `prefix`)
static void terminate(http_req_t* req, const char* at, size_t len) {
  ((char*)at)[len] = 0;
}


static int on_header_field(http_parser* parser, const char* at, size_t len) {
  http_req_t* req = (http_req_t*)parser->data;
  terminate(req, at, len);
  req->current_field = at;
  return 0;
}

static int on_header_value(http_parser* parser, const char* at, size_t len) {
  http_req_t* req = (http_req_t*)parser->data;
  terminate(req, at, len);
  http_headers_add(&req->headers, req->current_field, at, req->headers_complete); // allocate if the headers are complete as the buffer might have changed
  req->current_field = NULL;
  return 0;
}

static int on_url(http_parser* parser, const char* at, size_t len) {
  http_req_t* req = (http_req_t*)parser->data;
  terminate(req, at, len);
  req->url = at;
  return 0;
}


static int on_body(http_parser* parser, const char* at, size_t len) {
  http_req_t* req = (http_req_t*)parser->data;
  req->current_body = nodec_buf((char*)at, len);  // remember this body piece
  http_parser_pause(parser, 1);             // and pause the parser, returning from execute! (with parser->errno set to HPE_PAUSED)
  return 0;
}

static int on_headers_complete(http_parser* parser) {
  http_req_t* req = (http_req_t*)parser->data;
  req->headers_complete = true;
  return 0;
}

static int on_message_complete(http_parser* parser) {
  http_req_t* req = (http_req_t*)parser->data;
  req->complete = true;
  return 0;
}

// Clear and free all members of a request
static void http_req_clear(http_req_t* req) {
  http_headers_clear(&req->headers);
  if (req->current.base != NULL && req->current.base != req->prefix.base) nodec_free(req->current.base);
  if (req->prefix.base != NULL) nodec_free(req->prefix.base);
  memset(req, 0, sizeof(http_req_t));
}

void http_req_free(http_req_t* req) {
  http_req_clear(req);
  nodec_free(req);
}

void http_req_freev(lh_value reqv) {
  http_req_free((http_req_t*)lh_ptr_value(reqv));
}

static enum http_errno check_http_errno(http_parser* parser) {
  enum http_errno err = HTTP_PARSER_ERRNO(parser);
  if (err != HPE_OK && err != HPE_PAUSED) {  // pausing is ok!
    throw_http_err_str(HTTP_STATUS_BAD_REQUEST, http_errno_description(err));
  }
  return err;
}

http_req_t* async_http_req_alloc(uv_stream_t* stream)
{
  // start the read stream and read the headers (and at most 8Kb of data)
  nodec_read_start(stream, HTTP_MAX_HEADERS, HTTP_MAX_HEADERS, 0);

  // and read until the double empty line is seen  (\r\n\r\n); the end of the headers 
  size_t idx = 0;
  uv_buf_t buf = async_read_buf_including(stream, &idx, "\r\n\r\n", 4);
  if (idx==0 || buf.base==NULL || idx>HTTP_MAX_HEADERS) {
    if (buf.base!=NULL) nodec_free(buf.base);
    throw_http_err((idx>HTTP_MAX_HEADERS ? HTTP_STATUS_PAYLOAD_TOO_LARGE : HTTP_STATUS_BAD_REQUEST));
  }
  // set the default stream read capacity again for futher reading (=1Gb)
  nodec_set_read_max(stream, 0);

  // only if successful initialize a request object
  http_req_t* req = nodec_zero_alloc(http_req_t);
  {on_abort(http_req_freev, lh_value_ptr(req)) {
    req->stream = stream;
    req->current = buf;
    req->prefix = req->current; // set the prefix to the current buffer to keep it alive    
    http_parser_init(&req->parser, HTTP_REQUEST);
    req->parser.data = req;
    http_parser_settings_init(&req->parser_settings);
    req->parser_settings.on_header_field = &on_header_field;
    req->parser_settings.on_header_value = &on_header_value;
    req->parser_settings.on_headers_complete = &on_headers_complete;
    req->parser_settings.on_message_complete = &on_message_complete;
    req->parser_settings.on_body = &on_body;
    req->parser_settings.on_url = &on_url;

    // parse the headers
    size_t nread = http_parser_execute(&req->parser, &req->parser_settings, req->current.base, req->current.len);
    check_http_errno(&req->parser);

    // remember where we are at in the current buffer for further body reads 
    req->current_offset = nread;
  }}

  return req;
}

/*-----------------------------------------------------------------
Reading the body of a request
-----------------------------------------------------------------*/


// Read asynchronously a piece of the body; the return buffer is valid until
// the next read. Returns a null buffer when the end of the request is reached.
uv_buf_t async_http_req_read_body_buf(http_req_t* req)
{
  // if there is no current body ready: read another one.
  // (there might be an initial body due to the initial parse of the headers.)
  if (req->current_body.base == NULL)
  {
    // if we are done already, just return null
    if (req->complete) return nodec_buf_null();

    // if we exhausted our current buffer, read a new one
    if (req->current.base == NULL || req->current_offset >= req->current.len) {
      // deallocate current buffer first
      if (req->current.base != NULL) {
        if (req->current.base != req->prefix.base) { // don't deallocate the prefix with the headers
          nodec_free(req->current.base);
        }
        req->current = nodec_buf_null();
        req->current_offset = 0;
      }
      // and read a fresh buffer async from the stream
      req->current = async_read_buf(req->stream);
      if (req->current.base == NULL || req->current.len == 0) throw_http_err(HTTP_STATUS_BAD_REQUEST);
    }

    // we have a current buffer, parse a body piece (or read to eof)
    assert(req->current.base != NULL && req->current_offset < req->current.len);
    http_parser_pause(&req->parser, 0); // unpause
    size_t nread = http_parser_execute(&req->parser, &req->parser_settings, req->current.base + req->current_offset, req->current.len - req->current_offset);
    req->current_offset += nread;
    check_http_errno(&req->parser);

    // if no body now, something went wrong or we read to the end of the request without further bodies
    if (req->current_body.base == NULL) {
      if (req->complete) return nodec_buf_null();  // done parsing, no more body pieces
      throw_http_err_str(HTTP_STATUS_BAD_REQUEST, "couldn't parse request body");
    }
  }

  // We have a body piece ready, return it
  assert(req->current_body.base != NULL);
  uv_buf_t body = req->current_body;
  req->current_body = nodec_buf_null();
  return body;  // a view into our current buffer, valid until the next read
}


// Read asynchronously the entire body of the request. The caller is responsible for buffer deallocation.
// The initial_size can be 0 in which case the content_length or initially read buffer length is used.
uv_buf_t async_http_req_read_body(http_req_t* req, size_t initial_size) {
  uv_buf_t body = nodec_buf_null();
  {on_abort(nodec_free_bufrefv, lh_value_ptr(&body)) {
    size_t   offset = 0;
    // keep reading bufs into the target body buffer, reallocating as needed
    for (uv_buf_t buf = async_http_req_read_body_buf(req); buf.base != NULL; offset += buf.len) {
      if (buf.len + offset > body.len) { // always true on the first iteration        
                                         // the initial allocation is either the content length or the initially read buffer
                                         // this can't read too much as the stream is already limited separately
        uv_buf_len_t newlen;
        if (body.len > 0) {
          newlen = (body.len > 4*1024*1024 ? body.len + 4*1024*1024 : body.len*2);
        }
        else if (initial_size > 0) {
          newlen = (uv_buf_len_t)initial_size;
        }
        else {
          newlen = (req->parser.content_length > 0 ? (uv_buf_len_t)req->parser.content_length : buf.len);
        }
        if (newlen < body.len) lh_throw_errno(ENOMEM); // overflow
        body.base = nodec_realloc_n(body.base, newlen, uint8_t);
        body.len = newlen;
      }
      // we cannot avoid memcpy due to chunking which breaks up the body
      memcpy(body.base + offset, buf.base, buf.len);
    }
  }}
  return body;
}



/*-----------------------------------------------------------------
Helpers
-----------------------------------------------------------------*/

// Return the read only request URL
const char* http_req_url(http_req_t* req) {
  return req->url;
}

uint16_t http_req_version(http_req_t* req) {
  return (req->parser.http_major << 8 | req->parser.http_minor);
}

http_method_t http_req_method(http_req_t* req) {
  return (http_method_t)(req->parser.method);
}

uint64_t http_req_content_length(http_req_t* req) {
  return req->parser.content_length;
}

const char* http_req_header(http_req_t* req, const char* name) {
  return http_headers_lookup(&req->headers, name);
}

const char* http_req_header_next(http_req_t* req, const char** value, size_t* iter) {
  return http_headers_next(&req->headers, value, iter);
}


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


#ifdef _MSC_VER
# include <malloc.h>
# define alloca _alloca
#else
# include <alloca.h>
#endif


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
  h->value = strdup ? nodec_strdup(value) : value;
  h->_do_free = strdup;
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

typedef struct _http_in_t
{
  uv_stream_t*    stream; // the input stream
  http_parser     parser; // the request parser on the stream
  http_parser_settings parser_settings;

  bool            is_request;
  const char*     url;            // parsed url (for client request)
  http_status_t     status;         // parsed status (for server response)
  size_t          content_length; // real content length from headers
  http_headers_t  headers; // parsed headers; usually pointing into `prefix`
  uv_buf_t        prefix;  // the initially read buffer that holds all initial headers

  uv_buf_t        current;        // the last read buffer; starts equal to prefix
  size_t          current_offset; // parsed up to this point into the current buffer
  uv_buf_t        current_body;   // the last parsed body piece; each on_body pauses the parser so only one is needed
  const char*     current_field;  // during header parsing, holds the last seen header field

  bool            headers_complete;  // true if all initial headers have been parsed
  bool            complete;          // true if the whole message has been parsed
} http_in_t;

// Terminate header fields and values by modifying the read buffer in place. (in `prefix`)
static void terminate(http_in_t* req, const char* at, size_t len) {
  ((char*)at)[len] = 0;
}


static int on_header_field(http_parser* parser, const char* at, size_t len) {
  http_in_t* req = (http_in_t*)parser->data;
  terminate(req, at, len);
  req->current_field = at;
  return 0;
}

static int on_header_value(http_parser* parser, const char* at, size_t len) {
  http_in_t* req = (http_in_t*)parser->data;
  terminate(req, at, len);
  http_headers_add(&req->headers, req->current_field, at, req->headers_complete); // allocate if the headers are complete as the buffer might have changed
  if (_stricmp(req->current_field, "content-length")==0) {
    long long len = atoll(at);
    // printf("read content-length: %lli\n", len);
    if (len > 0 && len <= SIZE_MAX) req->content_length = (size_t)len;
  }
  req->current_field = NULL;
  return 0;
}

static int on_url(http_parser* parser, const char* at, size_t len) {
  http_in_t* req = (http_in_t*)parser->data;
  terminate(req, at, len);
  req->url = at;
  return 0;
}


static int on_status(http_parser* parser, const char* at, size_t len) {
  http_in_t* req = (http_in_t*)parser->data;
  terminate(req, at, len);
  long status = atol(at);
  if (status >= 100 && status < 600) {
    req->status = (http_status_t)status;
  }
  return 0;
}


static int on_body(http_parser* parser, const char* at, size_t len) {
  http_in_t* req = (http_in_t*)parser->data;
  terminate(req, at, len); // TODO: I think this is always safe since stream reads always over allocate by 1...
  req->current_body = nodec_buf((char*)at, len);  // remember this body piece
  http_parser_pause(parser, 1);             // and pause the parser, returning from execute! (with parser->errno set to HPE_PAUSED)
  return 0;
}

static int on_headers_complete(http_parser* parser) {
  http_in_t* req = (http_in_t*)parser->data;
  req->headers_complete = true;
  return 0;
}

static int on_message_complete(http_parser* parser) {
  http_in_t* req = (http_in_t*)parser->data;
  req->complete = true;
  return 0;
}

// Clear and free all members of a request
static void http_in_clear(http_in_t* req) {
  http_headers_clear(&req->headers);
  if (req->current.base != NULL && req->current.base != req->prefix.base) nodec_free(req->current.base);
  if (req->prefix.base != NULL) nodec_free(req->prefix.base);
  memset(req, 0, sizeof(http_in_t));
}

void http_in_clearv(lh_value reqv) {
  http_in_clear((http_in_t*)lh_ptr_value(reqv));
}

static enum http_errno check_http_errno(http_parser* parser) {
  enum http_errno err = HTTP_PARSER_ERRNO(parser);
  if (err != HPE_OK && err != HPE_PAUSED) {  // pausing is ok!
    throw_http_err_str(HTTP_STATUS_BAD_REQUEST, http_errno_description(err));
  }
  return err;
}

void http_in_init(http_in_t* in, uv_stream_t* stream, bool is_request)
{
  memset(in, 0, sizeof(http_in_t));
  in->stream = stream;
  in->is_request = is_request;
}

size_t async_http_in_read_headers(http_in_t* in ) 
{
  // start the read stream and read the headers (and at most 8Kb of data)
  nodec_read_start(in->stream, HTTP_MAX_HEADERS, HTTP_MAX_HEADERS, 0);

  // and read until the double empty line is seen  (\r\n\r\n); the end of the headers 
  size_t idx = 0;
  uv_buf_t buf = async_read_buf_including(in->stream, &idx, "\r\n\r\n", 4);
  if (idx == 0 || buf.base == NULL || idx > HTTP_MAX_HEADERS) {
    if (buf.base != NULL) nodec_free(buf.base);
    throw_http_err((idx > HTTP_MAX_HEADERS ? HTTP_STATUS_PAYLOAD_TOO_LARGE : HTTP_STATUS_BAD_REQUEST));
  }
  // set the default stream read capacity again for futher reading (=1Gb)
  nodec_set_read_max(in->stream, 0);
  //printf("\n\nraw prefix read: %s\n\n\n", buf.base);  // only print idx length here

  // only if successful initialize a request object
  in->current = buf;
  in->prefix = in->current; // set the prefix to the current buffer to keep it alive    
  http_parser_init(&in->parser, (in->is_request ? HTTP_REQUEST : HTTP_RESPONSE));
  in->parser.data = in;
  http_parser_settings_init(&in->parser_settings);
  in->parser_settings.on_header_field = &on_header_field;
  in->parser_settings.on_header_value = &on_header_value;
  in->parser_settings.on_headers_complete = &on_headers_complete;
  in->parser_settings.on_message_complete = &on_message_complete;
  in->parser_settings.on_body = &on_body;
  in->parser_settings.on_url = &on_url;
  in->parser_settings.on_status = &on_status;

  // parse the headers
  size_t nread = http_parser_execute(&in->parser, &in->parser_settings, in->current.base, in->current.len);
  check_http_errno(&in->parser);

  // remember where we are at in the current buffer for further body reads 
  in->current_offset = nread;

  return idx; // size of the headers
}

/*-----------------------------------------------------------------
Reading the body of a request
-----------------------------------------------------------------*/


// Read asynchronously a piece of the body; the return buffer is valid until
// the next read. Returns a null buffer when the end of the request is reached.
uv_buf_t async_http_in_read_body_buf(http_in_t* req)
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
      req->current.base[req->current.len] = 0;
      //printf("\n\nraw body read: %s\n\n\n", req->current.base);
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
  printf("read body part: len: %i\n", body.len);
  return body;  // a view into our current buffer, valid until the next read
}


// Read asynchronously the entire body of the request. The caller is responsible for buffer deallocation.
// The initial_size can be 0 in which case the content_length or initially read buffer length is used.
uv_buf_t async_http_in_read_body(http_in_t* req, size_t initial_size) {
  uv_buf_t result = nodec_buf_null();
  {with_on_abort_free_buf(body){
    size_t   offset = 0;
    // keep reading bufs into the target body buffer, reallocating as needed
    uv_buf_t buf;
    while ((buf = async_http_in_read_body_buf(req), buf.base != NULL)) {
      if (initial_size == 0) {
        initial_size = (http_in_content_length(req) > 0 ? (uv_buf_len_t)http_in_content_length(req) : buf.len);
      }
      nodec_buf_ensure_ex(&body, buf.len + offset, initial_size, 0);
      assert(body.len >= offset + buf.len);

      // we cannot avoid memcpy due to chunking which breaks up the body
      memcpy(body.base + offset, buf.base, buf.len);
      offset += buf.len;
      body.base[offset] = 0;  // always safe as bufs are allocated + 1 length
    }
    result = body;
  }}
  return result;
}



/*-----------------------------------------------------------------
Helpers
-----------------------------------------------------------------*/

// Return the read only request URL (only valid on server requests)
const char* http_in_url(http_in_t* req) {
  return req->url;
}

// Return the read only HTTP Status (only valid on server responses)
http_status_t http_in_status(http_in_t* req) {
  return req->status;
}

uint16_t http_in_version(http_in_t* req) {
  return (req->parser.http_major << 8 | req->parser.http_minor);
}

// Return the read only request HTTP method (only valid on server requests)
http_method_t http_in_method(http_in_t* req) {
  return (http_method_t)(req->parser.method);
}

size_t http_in_content_length(http_in_t* req) {
  return req->content_length;
}

const char* http_in_header(http_in_t* req, const char* name) {
  return http_headers_lookup(&req->headers, name);
}

const char* http_in_header_next(http_in_t* req, const char** value, size_t* iter) {
  return http_headers_next(&req->headers, value, iter);
}




/*-----------------------------------------------------------------
   HTTP response
-----------------------------------------------------------------*/
typedef struct _http_out_t {
  uv_stream_t* stream;
  uv_buf_t     head;
  size_t       head_offset;
} http_out_t;

void http_out_init(http_out_t* out, uv_stream_t* stream) {
  memset(out, 0, sizeof(http_out_t));
  out->stream = stream;
}

void http_out_init_server(http_out_t* out, uv_stream_t* stream, const char* server_name) {
  http_out_init(out, stream);
  http_out_add_header(out, "Server", server_name);
}

void http_out_init_client(http_out_t* out, uv_stream_t* stream, const char* host_name) {
  http_out_init(out, stream);
  http_out_add_header(out, "Host", host_name);
}


void http_out_clear(http_out_t* out) {
  nodec_bufref_free(&out->head);
  out->head_offset = 0;
}

void http_out_clearv(lh_value respv) {
  http_out_clear((http_out_t*)lh_ptr_value(respv));
}

void http_out_add_header(http_out_t* out, const char* field, const char* value) {
  size_t n = strlen(field);
  size_t m = strlen(value);
  size_t extra = n + m + 3; // :\r\n
  nodec_buf_ensure(&out->head, out->head_offset + extra);
  char* p = out->head.base + out->head_offset;
  size_t available = out->head.len - out->head_offset;
  strncpy_s(p, available, field, n);
  p[n] = ':';
  strncpy_s(p + n + 1, available - n - 1, value, m);
  strncpy_s(p + n + m + 1, available - n - m - 1, "\r\n", 2);
  out->head_offset += extra;
}

static void http_out_add_header_buf(http_out_t* out, uv_buf_t buf) {
  if (nodec_buf_is_null(buf)) return;
  nodec_buf_ensure(&out->head, out->head_offset + buf.len);
  size_t available = out->head.len - out->head_offset;
  memcpy(out->head.base + out->head_offset, buf.base, buf.len);
  out->head_offset += buf.len;
}

static void http_out_send_raw_str(http_out_t* out, const char* s) {
  async_write(out->stream, s);
}


static void http_out_send_raw_headers(http_out_t* out, uv_buf_t prefix, uv_buf_t postfix) {
  uv_buf_t buf = nodec_buf(out->head.base, out->head_offset);
  uv_buf_t bufs[3] = { prefix, buf, postfix };
  async_write_bufs(out->stream, bufs, 3);
  nodec_bufref_free(&out->head);
  out->head_offset = 0;
}

void http_out_send_headers(http_out_t* out, const char* prefix, const char* postfix) {
  http_out_send_raw_headers(out, nodec_buf_str(prefix), nodec_buf_str(postfix));
}

void http_out_send_status_headers(http_out_t* out, http_status_t status, bool end) {
  // send status
  if (status == 0) status = HTTP_STATUS_OK;
  char line[256];
  snprintf(line, 256, "HTTP/1.1 %i %s\r\n", status, nodec_http_status_str(status));
  line[255] = 0;
  http_out_send_headers(out, line, (end ? "Content-Length: 0\r\n\r\n" : NULL));
}

void http_out_send_request_headers(http_out_t* out, http_method_t method, const char* url, bool end) {
  char prefix[512];
  snprintf(prefix, 512, "%s %s HTTP/1.1\r\n", nodec_http_method_str(method), url);
  prefix[511] = 0;
  http_out_send_headers(out, prefix, (end ? "\r\n" : NULL));
}



static void http_out_send_bufs(http_out_t* out, uv_buf_t bufs[], size_t count, const char* prefix_fmt, const char* postfix) {
  // pre and post fix the buffers, and calculate the total
  uv_buf_t* xbufs = alloca((count + 2) * sizeof(uv_buf_t));
  size_t total = 0;
  for (size_t i = 0; i < count; i++) {
    total += bufs[i].len;
    if (total < bufs[i].len) lh_throw_errno(EOVERFLOW);
    xbufs[i + 1] = bufs[i];
  }
  // create pre- and postfix
  char prefix[64];
  snprintf(prefix, 64, prefix_fmt, total);
  xbufs[0] = nodec_buf(prefix, strlen(prefix));
  xbufs[count + 1] = nodec_buf_str(postfix);
  // and write it out as a chunk
  async_write_bufs(out->stream, xbufs, count + 2);
}


// Send entire body at once
void http_out_send_body_bufs(http_out_t* out, uv_buf_t bufs[], size_t count) {
  http_out_send_bufs(out, bufs, count, "Content-Length: %l\r\n\r\n", ""); // decimal length
}

void http_out_send_body_buf(http_out_t* out, uv_buf_t buf) {
  http_out_send_body_bufs(out, &buf, 1);
}

void http_out_send_body(http_out_t* out, const char* s) {
  http_out_send_body_buf(out, nodec_buf_str(s));
}



// -----------------------------------------------------------------------
// Send body in chunks

// Start chunked body send
void http_out_send_chunked_start(http_out_t* out) {
  http_out_send_raw_str(out, "Transfer-Encoding: chunked\r\n\r\n");
}

void http_out_send_chunk_bufs(http_out_t* out, uv_buf_t bufs[], size_t count) {
  http_out_send_bufs(out, bufs, count, "%X\r\n", "\r\n"); // hexadecimal length
}

void http_out_send_chunk_buf(http_out_t* out, uv_buf_t buf) {
  http_out_send_chunk_bufs(out, &buf, 1);
}

void http_out_send_chunk(http_out_t* out, const char* s) {
  http_out_send_chunk_buf(out, nodec_buf_str(s));
}

void http_out_send_chunked_end(http_out_t* out) {
  http_out_send_chunk_buf(out, nodec_buf_null());  // final 0 length message
}




/*-----------------------------------------------------------------
  HTTP server function
-----------------------------------------------------------------*/


typedef struct _server_args_t {
  nodec_http_servefun* servefun;
  lh_value arg;
} server_args_t;

static void http_serve(int id, uv_stream_t* client, lh_value argsv) {
  server_args_t* args = (server_args_t*)lh_ptr_value(argsv);
  http_in_t http_in;
  http_in_init(&http_in, client, true);
  {defer(http_in_clearv, lh_value_any_ptr(&http_in)) {
    http_out_t http_out;
    http_out_init_server(&http_out, client, "NodeC/0.1");
    {defer(http_out_clearv, lh_value_any_ptr(&http_out)) {
      async_http_in_read_headers(&http_in);
      args->servefun(id, &http_in, &http_out, args->arg);
    }}
  }}
}


void async_http_server_at(const struct sockaddr* addr, int backlog,
  int n, uint64_t timeout,
  nodec_http_servefun* servefun,
  lh_value arg)
{
  server_args_t args = { servefun, arg };
  async_tcp_server_at(addr, backlog, n, timeout, http_serve,
    &async_write_http_exnv, lh_value_any_ptr(&args));
}

lh_value async_http_connect(const char* host, http_connect_fun* connectfun, lh_value arg) {
  lh_value result = lh_value_null;
  uv_stream_t* conn = async_tcp_connect(host, "http");
  {with_stream(conn) {
    http_in_t in;
    http_in_init(&in, conn, false);
    {defer(http_in_clearv, lh_value_any_ptr(&in)) {
      http_out_t out;
      http_out_init_client(&out, conn, host);
      {defer(http_out_clearv, lh_value_any_ptr(&out)) {
        result = connectfun(&in, &out, arg);
      }}
    }}
  }}
  return result;
}
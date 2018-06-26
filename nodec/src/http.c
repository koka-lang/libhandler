/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "nodec.h"
#include "nodec-internal.h"
#include <uv.h>
#include <assert.h>

/*-----------------------------------------------------------------
  HTTP Request
-----------------------------------------------------------------*/
/*
typedef enum _http_method_t {
  HTTP_UNKNOWN_METHOD = 0,
  HTTP_GET,
  HTTP_HEAD,
  HTTP_POST,
  HTTP_PUT,
  HTTP_DELETE,
  HTTP_CONNECT,
  HTTP_OPTIONS,
  HTTP_TRACE
} http_method_t;

typedef struct _http_header_t {
  const char* name;
  const char* value;
} http_header_t;


typedef struct _http_headers_t {
  size_t  count; // how many are there
  size_t  size;  // how big is our array
  const http_header_t*  headers;  // realloc on demand, perhaps start with size 8 and do +8 as it comes?  
} http_headers_t;

static void http_headers_free(http_headers_t* headers);
static void http_headers_add(http_headers_t* headers, const char* name, const char* value);
static const char* http_headers_lookup(http_headers_t* headers, const char* name);
static size_t http_headers_count(http_headers_t* headers);
static const http_header_t* http_headers_at(http_headers_t* headers);


typedef struct _http_request_t {
  // special fields
  http_method_t   method;
  const char*     url;
  const char*     http_version;
  size_t          content_length;
  http_headers_t  headers;  
} http_request_t;

static http_request_t* http_request_alloc() {
  return nodec_zero_alloc(http_request_t);
}

const char*   http_request_url(http_request_t* req);
http_method_t http_request_method(http_request_t* req);
const char*   http_request_header(http_request_t* req, const char* name);
// .. etc

static void http_request_add_header(http_request_t* req, const char* name, const char* value);
*/

/*-----------------------------------------------------------------
  HTTP errors|
-----------------------------------------------------------------*/
const char* http_error_headers =
"HTTP/1.1 %i %s\r\n"
"Server : NodeC\r\n"
"Content-Length : %i\r\n"
"Content-Type : text/html; charset=utf-8\r\n"
"Connection : Closed\r\n"
"\r\n";

const char* http_error_body =
"<!DOCTYPE html>"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"</head>\n"
"<body>\n"
"  <p>Error %i (%s)%s%s.</p>\n"
"</body>\n"
"</html>\n";


typedef struct _http_err_reason {
  http_status status;
  const char* reason;
} http_err_reason;

static http_err_reason http_reasons[] = {
  { 200, "OK" },
  { 500, "Internal Server Error" },
  { 400, "Bad Request" },
  { 401, "Unauthorized" },
  { 100, "Continue" },
  { 101, "Switching Protocols" },
  { 201, "Created" },
  { 202, "Accepted" },
  { 203, "Non - Authoritative Information" },
  { 204, "No Content" },
  { 205, "Reset Content" },
  { 206, "Partial Content" },
  { 300, "Multiple Choices" },
  { 301, "Moved Permanently" },
  { 302, "Found" },
  { 303, "See Other" },
  { 304, "Not Modified" },
  { 305, "Use Proxy" },
  { 307, "Temporary Redirect" },
  { 402, "Payment Required" },
  { 403, "Forbidden" },
  { 404, "Not Found" },
  { 405, "Method Not Allowed" },
  { 406, "Not Acceptable" },
  { 407, "Proxy Authentication Required" },
  { 408, "Request Time - out" },
  { 409, "Conflict" },
  { 410, "Gone" },
  { 411, "Length Required" },
  { 412, "Precondition Failed" },
  { 413, "Request Entity Too Large" },
  { 414, "Request - URI Too Large" },
  { 415, "Unsupported Media Type" },
  { 416, "Requested range not satisfiable" },
  { 417, "Expectation Failed" },
  { 501, "Not Implemented" },
  { 502, "Bad Gateway" },
  { 503, "Service Unavailable" },
  { 504, "Gateway Time - out" },
  { 505, "HTTP Version not supported" },
  { -1, NULL }
};

static void async_write_http_err(uv_stream_t* client, http_status code, const char* msg) {
  const char* reason = "Unknown";
  for (http_err_reason* r = http_reasons; r->reason != NULL; r++) {
    if (r->status == code) {
      reason = r->reason;
      break;
    }
  }
  char body[256];
  snprintf(body, 255, http_error_body, code, reason, (msg == NULL ? "" : ": "), (msg == NULL ? "" : msg));
  body[255] = 0;
  char headers[256];
  snprintf(headers, 255, http_error_headers, code, reason, strlen(body));
  headers[255] = 0;
  const char* strs[2] = { headers, body };
  fprintf(stderr, "HTTP error: %i (%s): %s\n\n", code, reason, (msg == NULL ? "" : msg));
  async_write_strs(client, strs, 2);
}

void lh_throw_http_err_str(http_status status, const char* msg) {
  lh_throw_str(UV_EHTTP - status, msg);
}

void lh_throw_http_err_strdup(http_status status, const char* msg) {
  lh_throw_strdup(UV_EHTTP - status, msg);
}

void lh_throw_http_err(http_status status) {
  lh_throw_http_err_str(status, NULL);
}


static lh_value async_write_http_exnv(lh_value exnv) {
  lh_exception* exn = (lh_exception*)lh_ptr_value(exnv);
  if (exn == NULL || exn->data == NULL) return lh_value_null;
  uv_stream_t* client = exn->data;
  http_status status = 500;
  if (exn->code < UV_EHTTP && exn->code >(UV_EHTTP - 600)) {
    status = -(exn->code - UV_EHTTP);
  }
  async_write_http_err(client, status, exn->msg);
  return lh_value_null;
}


void async_http_server_at(const struct sockaddr* addr, int backlog, int n, uint64_t timeout, nodec_tcp_servefun* servefun) {
  async_tcp_server_at(addr, backlog, n, timeout, servefun, &async_write_http_exnv);
}
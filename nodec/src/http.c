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
  // Use HTTP_STATUS_MAP from <http_parser.h>
  #define XX(num, name, string) { num, #string },
  HTTP_STATUS_MAP(XX)
  #undef XX
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

void throw_http_err_str(http_status status, const char* msg) {
  lh_throw_str(UV_EHTTP - status, msg);
}

void throw_http_err_strdup(http_status status, const char* msg) {
  lh_throw_strdup(UV_EHTTP - status, msg);
}

void throw_http_err(http_status status) {
  throw_http_err_str(status, NULL);
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
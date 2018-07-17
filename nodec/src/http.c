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



const char* nodec_http_status_str(http_status_t code) {
  return http_status_str(code);
}

const char* nodec_http_method_str(http_method_t method) {
  return http_method_str(method);
}


static void async_write_http_err(uv_stream_t* client, http_status_t code, const char* msg) {
  const char* reason = nodec_http_status_str(code);
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

void throw_http_err_str(http_status_t status, const char* msg) {
  lh_throw_str(UV_EHTTP - status, msg);
}

void throw_http_err_strdup(http_status_t status, const char* msg) {
  lh_throw_strdup(UV_EHTTP - status, msg);
}

void throw_http_err(http_status_t status) {
  throw_http_err_str(status, NULL);
}

lh_value async_write_http_exnv(lh_value exnv) {
  lh_exception* exn = (lh_exception*)lh_ptr_value(exnv);
  if (exn == NULL || exn->data == NULL) return lh_value_null;
  uv_stream_t* client = exn->data;
  http_status_t status = 500;
  if (exn->code < UV_EHTTP && exn->code >(UV_EHTTP - 600)) {
    status = -(exn->code - UV_EHTTP);
  }
  async_write_http_err(client, status, exn->msg);
  return lh_value_null;
}




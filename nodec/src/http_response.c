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

#ifdef _MSC_VER
# include <malloc.h>
# define alloca _alloca
#else
# include <alloca.h>
#endif

/*-----------------------------------------------------------------
 
-----------------------------------------------------------------*/

void http_resp_init(http_response_t* resp, uv_stream_t* stream ) {
  memset(resp, 0, sizeof(http_response_t));
  resp->stream = stream;
}

void http_resp_clear(http_response_t* resp) {
  nodec_bufref_free(&resp->head);
  resp->head_offset = 0;
}

void http_resp_clearv(lh_value respv) {
  http_resp_clear((http_response_t*)lh_ptr_value(respv));
}

void http_resp_add_header(http_response_t* resp, const char* field, const char* value) {
  size_t n = strlen(field);
  size_t m = strlen(value);
  size_t extra = n + m + 3; // :\r\n
  nodec_buf_ensure(&resp->head, resp->head_offset + extra);
  char* p = resp->head.base + resp->head_offset;
  size_t available = resp->head.len - resp->head_offset;
  strncpy_s(p, available, field, n);
  p[n] = ':';
  strncpy_s(p + n + 1, available - n - 1, value, m);
  strncpy_s(p + n + m + 1, available - n - m - 1, "\r\n", 2);
  resp->head_offset += extra;
}

static void http_resp_add_header_buf(http_response_t* resp, uv_buf_t buf ) {
  if (nodec_buf_is_null(buf)) return;
  nodec_buf_ensure(&resp->head, resp->head_offset + buf.len);
  size_t available = resp->head.len - resp->head_offset;
  memcpy(resp->head.base + resp->head_offset, buf.base, buf.len);
  resp->head_offset += buf.len;
}

static void http_resp_send_raw_str(http_response_t* resp, const char* s) {
  async_write(resp->stream, s);
}


static void http_resp_send_raw_headers(http_response_t* resp, uv_buf_t prefix, uv_buf_t postfix ) {
  uv_buf_t buf = nodec_buf(resp->head.base, resp->head_offset);
  uv_buf_t bufs[3] = { prefix, buf, postfix };
  async_write_bufs( resp->stream, bufs, 3 );
  nodec_bufref_free(&resp->head);
  resp->head_offset = 0;
}

void http_resp_send_headers(http_response_t* resp, const char* prefix, const char* postfix ) {
  http_resp_send_raw_headers(resp, nodec_buf_str(prefix), nodec_buf_str(postfix));
}

void http_resp_send_status_headers(http_response_t* resp, http_status status, bool end) {
  // send status
  if (status == 0) status = HTTP_STATUS_OK;
  char line[256];
  snprintf(line, 256, "HTTP/1.1 %i %s\r\nServer : NodeC/0.1\r\n", status, nodec_http_get_reason(status));
  line[255] = 0;
  http_resp_send_headers(resp, line, (end ? "\r\n" : NULL));
}

void http_resp_send_request_headers(http_response_t* resp, http_method_t method, const char* url, const char* host, bool end) {
  char prefix[512];
  snprintf(prefix, 512, "%s %s HTTP/1.1\r\nHost: %s\r\n", http_method_str(method), url, host);
  prefix[511] = 0;
  http_resp_send_headers(resp, prefix, (end ? "\r\n" : NULL));
}



static void http_resp_send_bufs(http_response_t* resp, uv_buf_t bufs[], size_t count, const char* prefix_fmt, const char* postfix) {
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
  async_write_bufs(resp->stream, xbufs, count+2);
}


// Send entire body at once
void http_resp_send_body_bufs(http_response_t* resp, uv_buf_t bufs[], size_t count) {
  http_resp_send_bufs(resp, bufs, count, "Content-Length: %l\r\n\r\n", ""); // decimal length
}

void http_resp_send_body_buf(http_response_t* resp, uv_buf_t buf) {
  http_resp_send_body_bufs(resp, &buf, 1);
}

void http_resp_send_body(http_response_t* resp, const char* s) {
  http_resp_send_body_buf(resp, nodec_buf_str(s));
}



// -----------------------------------------------------------------------
// Send body in chunks

// Start chunked body send
void http_resp_send_chunked_start(http_response_t* resp) {
  http_resp_send_raw_str(resp, "Transfer-Encoding: chunked\r\n\r\n");
}

void http_resp_send_chunk_bufs(http_response_t* resp, uv_buf_t bufs[], size_t count) {
  http_resp_send_bufs(resp, bufs, count, "%X\r\n", "\r\n"); // hexadecimal length
}

void http_resp_send_chunk_buf(http_response_t* resp, uv_buf_t buf) {
  http_resp_send_chunk_bufs(resp, &buf, 1);
}

void http_resp_send_chunk(http_response_t* resp, const char* s) {
  http_resp_send_chunk_buf(resp, nodec_buf_str(s));
}

void http_resp_send_chunked_end(http_response_t* resp) {
  http_resp_send_chunk_buf(resp, nodec_buf_null());  // final 0 length message
}





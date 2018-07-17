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
 
-----------------------------------------------------------------*/

typedef struct _http_response_t {
  uv_stream_t* stream;
  uv_buf_t     head;
  size_t       head_offset;
} http_response_t;


void http_resp_clear(http_response_t* resp) {
  nodec_bufref_free(&resp->head);
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
  strncpy_s(p + n + 1, available - n -1, value, m);
  strncpy_s(p + n + m + 1, available - n - m - 1, "\r\n", 2);
  resp->head_offset += extra;
}

static void http_resp_add_str(http_response_t* resp, const char* s ) {
  size_t n = (s == NULL ? 0 : strlen(s));
  if (n == 0) return;
  nodec_buf_ensure(&resp->head, resp->head_offset + n);
  size_t available = resp->head.len - resp->head_offset;
  strncpy_s(resp->head.base + resp->head_offset, available, s, n);
  resp->head_offset += n;
}


void http_resp_send_headers(http_response_t* resp, http_status status) {
  // send status
  if (status == 0) status = HTTP_STATUS_OK;
  char line[256];
  snprintf(line, 256, "HTTP/1.1 %i %s\r\nServer : NodeC/0.1\r\n", status, nodec_http_get_reason(status));
  line[255] = 0;
  async_write(resp->stream, line);
  // send headers (including terminating newline pair)
  http_resp_add_str(resp, "\r\n");
  if (!nodec_buf_is_null(resp->head ) && resp->head_offset > 0) {
    uv_buf_t buf = nodec_buf(resp->head.base, resp->head_offset);
    async_write_buf(resp->stream, buf);
  }
}

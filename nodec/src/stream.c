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

// Await a shutdownn request
static int asyncx_await_shutdown(uv_shutdown_t* req) {
  return asyncx_await((uv_req_t*)req);
}

static void async_await_shutdown(uv_shutdown_t* req) {
  check_uv_err(asyncx_await_shutdown(req));
}

static void _async_shutdown_cb(uv_shutdown_t* req, int status) {
  _async_plain_cb((uv_req_t*)req, status);
}


static void _close_handle_cb(uv_handle_t* h) {
  nodec_free(h);
}

void nodec_handle_free(uv_handle_t* h) {
  // Todo: this is "wrong" as the callback is called outside of
  // our framework (i.e. we should do an await). 
  // but we're ok since the callback does just a free.
  uv_close(h, _close_handle_cb);
}

void nodec_stream_free(uv_stream_t* stream) {
  nodec_handle_free((uv_handle_t*)stream);
}

void async_shutdown(uv_stream_t* stream) {
  if (stream==NULL) return;
  if (stream->write_queue_size>0) {
    {with_alloc(uv_shutdown_t, req) {
      check_uv_err(uv_shutdown(req, stream, &_async_shutdown_cb));
    }}
  }
  nodec_stream_free(stream);
}
/*
static void _read_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  
}

static void _read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {

}

ssize_t async_read(uv_stream_t* stream, uv_buf_t* buffer) {
  stream->data = buffer;
  uv_read_start(stream, _read_alloc_cb, _read_cb);
}
*/
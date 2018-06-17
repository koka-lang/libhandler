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


#ifdef _MSC_VER
# include <malloc.h>
# define alloca _alloca
#else
# include <alloca.h>
#endif

/* ----------------------------------------------------------------------------
  Stream requests for reading
-----------------------------------------------------------------------------*/
typedef struct _stream_req_t {
  uv_req_t  req;  // must be the first element!
  uv_buf_t  buf;
  ssize_t   nread;
  int       err;
} stream_req_t;


static void stream_req_resume(stream_req_t* req, uv_stream_t* stream) {
  _async_plain_cb(&req->req, req->err);
}


/* ----------------------------------------------------------------------------
  Await shutdown
-----------------------------------------------------------------------------*/
static int asyncx_await_shutdown(uv_shutdown_t* req) {
  return asyncx_await((uv_req_t*)req);
}

static void async_await_shutdown(uv_shutdown_t* req) {
  check_uv_err(asyncx_await_shutdown(req));
}

static void _async_shutdown_cb(uv_shutdown_t* req, int status) {
  _async_plain_cb((uv_req_t*)req, status);
}


/* ----------------------------------------------------------------------------
  Await write requests
-----------------------------------------------------------------------------*/
static void _async_write_cb(uv_write_t* req, int status) {
  _async_plain_cb((uv_req_t*)req, status);
}

static void async_await_write(uv_write_t* req) {
  async_await((uv_req_t*)req);
}


/* ----------------------------------------------------------------------------
  Handle management
-----------------------------------------------------------------------------*/

static void _close_handle_cb(uv_handle_t* h) {
  if ((h->type == UV_STREAM || h->type==UV_TCP) && h->data!=NULL) {
    nodec_free(h->data); // stream_req_t
  }
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


/* ----------------------------------------------------------------------------
  Reading from a stream
-----------------------------------------------------------------------------*/

static void _read_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  stream_req_t* req = (stream_req_t*)handle->data;
  *buf = req->buf;
}

static void _read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  if (nread == 0) {
    // EAGAIN or EWOULDBLOCK, not EOF
    // do nothing?
  }
  else if (nread > 0) {
    // data available
    stream_req_t* req = (stream_req_t*)stream->data;
    req->nread += nread;
    req->buf = *buf;
    stream_req_resume(req,stream);
  }
  else if (nread < 0) {
    // done reading (error or UV_EOF)
    stream_req_t* req = (stream_req_t*)stream->data;
    req->err = (int)(nread==UV_EOF ? 0 : nread);
    stream_req_resume(req,stream);
  }
}

ssize_t async_read(uv_stream_t* stream, uv_buf_t buffer, ssize_t offset ) {
  stream_req_t* req = (stream_req_t*)stream->data;
  if (req==NULL) {
    stream->data = req = nodec_alloc(stream_req_t); // now owned by stream
    nodec_zero(stream_req_t,req);
    int err = uv_read_start(stream, _read_alloc_cb, _read_cb);
    if (err==UV_EOF) {
      // directly invoked callback! todo: can this ever happen?
      return 0;
    }
    check_uv_err(err);
  }
  req->buf.base = (offset>=buffer.len ? NULL : buffer.base + offset);
  req->buf.len = (ULONG)(offset >= buffer.len ? 0 : buffer.len - offset);
  async_await(&req->req);
  ssize_t nread = req->nread;
  nodec_zero(stream_req_t, req);
  return nread;
}

ssize_t async_read_full(uv_stream_t* stream, uv_buf_t* buffer) {
  if (buffer->base == NULL) {
    if (buffer->len == 0) buffer->len = 8 * 1024;
    buffer->base = nodec_malloc(buffer->len + 1);
  }
  ssize_t nread = 0;
  ssize_t total = 0;
  while ((nread = async_read(stream, *buffer, total)) > 0) {
    total += nread;
    if (buffer->len <= total) {
      //realloc 
      // todo: check newsize overflow?
      ssize_t newsize = (buffer->len > (1024 * 1024) ? buffer->len + (1024 * 1024) : buffer->len * 2);
      buffer->base = (char*)nodec_realloc(buffer->base, newsize + 1);
      buffer->len = (ULONG)newsize;
    }
  }
  ((int8_t*)buffer->base)[total] = 0;  // safe as we allocate always +1
  return total;
}


/* ----------------------------------------------------------------------------
  Writing to a stream
-----------------------------------------------------------------------------*/

void async_write(uv_stream_t* stream, const char* s) {
  if (s==NULL) return;
  async_write_data(stream, s, strlen(s));
}

void async_write_strs(uv_stream_t* stream, const char* strings[], ssize_t string_count) {
  if (strings==NULL||string_count <= 0) return;
  uv_buf_t* bufs = alloca(string_count*sizeof(uv_buf_t));
  for (ssize_t i = 0; i < string_count; i++) {
    bufs[i] = uv_buf_init((char*)strings[i], (unsigned)(strings[i]!=NULL ? strlen(strings[i]) : 0));
  }
  async_write_bufs(stream, bufs, string_count);
}

void async_write_data(uv_stream_t* stream, const void* data, ssize_t len) {
  uv_buf_t buf = uv_buf_init((void*)data, (unsigned)len);
  async_write_buf(stream, buf);
}

void async_write_buf(uv_stream_t* stream, uv_buf_t buf) {
  async_write_bufs(stream, &buf, 1);
}

void async_write_bufs(uv_stream_t* stream, uv_buf_t bufs[], ssize_t buf_count) {
  if (bufs==NULL || buf_count<=0) return;
  {with_zalloc(uv_write_t, req) {    
    // Todo: verify it is ok to have bufs on the stack or if we need to heap alloc them first for safety
    check_uv_err(uv_write(req, stream, bufs, buf_count, &_async_write_cb));
    async_await_write(req);
  }}
}
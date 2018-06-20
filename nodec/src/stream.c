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

#if (defined(_WIN32) || defined(_WIN64))
typedef ULONG  uv_buf_len_t;
#else
typedef size_t uv_buf_len_t;
#endif


uv_buf_t nodec_buf(const void* data, size_t len) {
  return uv_buf_init((char*)data, (uv_buf_len_t)(len));
}

/* ----------------------------------------------------------------------------
  Stream requests for reading
-----------------------------------------------------------------------------*/

// We use our own request for waiting on stream handle reads
typedef struct _stream_req_t {
  uv_req_t* uvreq;   
  uv_buf_t  buf;    // the bufffer to write to
  size_t    nread;  // bytes read
  uverr  err;       // error code
} stream_req_t;

// Resume to the `async_await_stream`
static void stream_req_resume(stream_req_t* req, uv_stream_t* stream) {
  async_req_resume(req->uvreq, req->err);
}

static void async_await_stream(stream_req_t* req) {
  async_await(req->uvreq);
}

/* ----------------------------------------------------------------------------
  Await shutdown
-----------------------------------------------------------------------------*/

static void async_await_shutdown(uv_shutdown_t* req) {
  async_await((uv_req_t*)req);
}

static void async_shutdown_resume(uv_shutdown_t* req, uverr status) {
  async_req_resume((uv_req_t*)req, status);
}


/* ----------------------------------------------------------------------------
  Await write requests
-----------------------------------------------------------------------------*/
static void async_await_write(uv_write_t* req) {
  async_await((uv_req_t*)req);
}

static void async_write_resume(uv_write_t* req, uverr status) {
  async_req_resume((uv_req_t*)req, status);
}



/* ----------------------------------------------------------------------------
  Handle management
-----------------------------------------------------------------------------*/

static void _close_handle_cb(uv_handle_t* h) {
  if ((h->type == UV_STREAM || h->type==UV_TCP) && h->data!=NULL) {
    nodec_req_free(((stream_req_t*)h->data)->uvreq);
    nodec_free(h->data);
  }
  nodec_free(h);
}

void nodec_handle_free(uv_handle_t* h) {
  // Todo: this is philosophically "wrong" as the callback is called outside of
  // our framework (i.e. we should do an await). 
  // but we're ok since the callback does just a free.
  if (h != NULL && !uv_is_closing(h)) {
    uv_close(h, _close_handle_cb);
  }
  else {
    _close_handle_cb(h);
  }
}   

void nodec_stream_free(uv_stream_t* stream) {
  nodec_handle_free((uv_handle_t*)stream);  
}

void async_shutdown(uv_stream_t* stream) {
  if (stream==NULL) return;
  if (stream->write_queue_size>0) {
    {with_req(uv_shutdown_t, req) {
      check_uv_err(uv_shutdown(req, stream, &async_shutdown_resume));
      async_await_shutdown(req);
    }}
  } 
  nodec_stream_free(stream);
}

void async_shutdownv(lh_value streamv) {
  async_shutdown((uv_stream_t*)lh_ptr_value(streamv));
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
    assert(req != NULL);
    if (req != NULL) {
      req->nread += nread;
      req->buf = *buf;
      stream_req_resume(req, stream);
    }
  }
  else if (nread < 0) {
    // done reading (error or UV_EOF)
    stream_req_t* req = (stream_req_t*)stream->data;
    assert(req != NULL);
    if (req != NULL) {
      req->err = (uverr)(nread == UV_EOF ? 0 : nread);
      stream_req_resume(req, stream);
    }
  }
}


static _stream_req_zero(lh_value sreqv) {
  nodec_zero(stream_req_t, (stream_req_t*)lh_ptr_value(sreqv));
}

// Most primitive: read from `stream` to a pre-allocated `buffer` starting at `offset`.
size_t async_read_buf(uv_stream_t* stream, uv_buf_t buffer, size_t offset ) {
  stream_req_t* req = (stream_req_t*)stream->data;
  if (req==NULL) {
    stream->data = req = nodec_zalloc(stream_req_t); // now owned by stream
    int err = uv_read_start(stream, _read_alloc_cb, _read_cb);
    if (err==UV_EOF) {
      // directly invoked callback! todo: can this ever happen?
      return 0;
    }
    check_uv_err(err);
  }
  req->buf = (offset>=buffer.len ? nodec_buf(NULL, 0) : nodec_buf(buffer.base+offset, buffer.len - offset));
  size_t nread = 0;
  {defer(&_stream_req_zero, lh_value_ptr(req)) {
    {with_free_req(uv_req_t, uvreq) {
      req->uvreq = uvreq;
      async_await_stream(req);
    }}
    nread = req->nread;
  }}
  return nread;
}

// Read at most `max_len` or upto end-of-data from `stream` into a buffer `buffer`.
// (Re)allocates as needed starting with `initial_size` if the initial `buffer` was empty.
// Reallocates at the end too to fit exactly the data that was read. The buffer memory
// will be one more byte than `buffer->len` and it is used to zero terminate the data.
size_t async_read(uv_stream_t* stream, uv_buf_t* buffer, size_t max_len, size_t initial_size ) {
  const size_t max_increase = 4*1024*1024;     // 4MB
  if (max_len==0) max_len = 1024*1024*1024;    // 1GB
  if (initial_size==0) initial_size = 8*1024; //  8KB
  if (buffer->base == NULL) {
    if (buffer->len == 0) {
      buffer->len = (uv_buf_len_t)(max_len < initial_size ? max_len : initial_size);
    }
    buffer->base = nodec_malloc(buffer->len + 1);
  }
  else {
    buffer->len--; // space for the final zero
  }
  size_t nread = 0;
  size_t total = 0;
  while (total < max_len && (nread = async_read_buf(stream, *buffer, total)) > 0) {
    total += nread;
    if (buffer->len <= total && buffer->len < max_len) {
      //realloc 
      // todo: check newsize overflow?
      size_t newsize = (buffer->len > max_increase ? buffer->len + max_increase : buffer->len * 2);      
      if (newsize > max_len) newsize = max_len;
      *buffer = nodec_buf(nodec_realloc(buffer->base, newsize + 1),newsize);
    }
  }
  buffer->base[total] = 0;  // safe as we allocate always +1
  if (buffer->len>total) {
    *buffer = nodec_buf(nodec_realloc(buffer->base, total+1),total); // reduce allocated area
  }
  return total;
}

// Read the initial data that is available upto `max_len` and return it as a string.
// Can keep reading until `*nread` becomes `0`. 
// The returned string is heap allocated and never NULL (and the caller should
// `nodec_free` it.
char* async_read_chunk(uv_stream_t* stream, size_t max_len, size_t* nread) {
  if (max_len==0) max_len = 8*1024;
  uv_buf_t buf = nodec_buf( nodec_nalloc(max_len+1, char), max_len);
  size_t n = async_read_buf(stream, buf, 0);
  buf.base[n] = 0;
  if (buf.len>n) {
    buf = nodec_buf(nodec_realloc(buf.base, n+1), n); // reduce allocated area
  }
  if (nread != NULL) *nread = n;
  return buf.base;
}

// Read upto `max_len` bytes or to the end-of-data as a string.
char* async_read_str(uv_stream_t* stream, size_t max_len, size_t* nread) {
  uv_buf_t buf = uv_buf_init(NULL, 0);
  size_t n = async_read(stream, &buf, max_len, 0);
  if (nread != NULL) *nread = n;
  return buf.base;
}


/* ----------------------------------------------------------------------------
  Writing to a stream
-----------------------------------------------------------------------------*/

void async_write(uv_stream_t* stream, const char* s) {
  if (s==NULL) return;
  async_write_data(stream, s, strlen(s));
}

void async_write_strs(uv_stream_t* stream, const char* strings[], unsigned int string_count) {
  if (strings==NULL||string_count <= 0) return;
  uv_buf_t* bufs = alloca(string_count*sizeof(uv_buf_t));
  for (unsigned int i = 0; i < string_count; i++) {
    bufs[i] = nodec_buf(strings[i], (strings[i]!=NULL ? strlen(strings[i]) : 0));
  }
  async_write_bufs(stream, bufs, string_count);
}

void async_write_data(uv_stream_t* stream, const void* data, size_t len) {
  uv_buf_t buf = nodec_buf(data, len);
  async_write_buf(stream, buf);
}

void async_write_buf(uv_stream_t* stream, uv_buf_t buf) {
  async_write_bufs(stream, &buf, 1);
}

void async_write_bufs(uv_stream_t* stream, uv_buf_t bufs[], unsigned int buf_count) {
  if (bufs==NULL || buf_count<=0) return;
  {with_zalloc(uv_write_t, req) {    
    // Todo: verify it is ok to have bufs on the stack or if we need to heap alloc them first for safety
    check_uv_err(uv_write(req, stream, bufs, buf_count, &async_write_resume));
    async_await_write(req);
  }}
}
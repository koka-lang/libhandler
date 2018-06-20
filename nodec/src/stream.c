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

/*
typedef struct _read_stream_t {
  uv_stream_t* stream;  // backlink (stream->data == this)
  size_t       read_max;
  size_t       read_chunk;
  uv_buf_t*    read_bufs;
  size_t       read_bufs_count;
  size_t       read_bufs_size;
  size_t       offset;  // offset into the first buffer
  size_t       available;
  size_t       total;
  uverr        err;
  bool         eof;
  uv_req_t*    req;
} read_stream_t;

static void read_stream_free(read_stream_t* rs) {
  if (rs == NULL) return;
  for (size_t i = 0; i < rs->read_bufs_count; i++) {
    nodec_free(rs->read_bufs[i].base);
  }
  nodec_free(rs->read_bufs);
  nodec_free(rs);
}

static void read_stream_push(read_stream_t* rs, const uv_buf_t* buf, size_t nread) {
  if (nread == 0 || buf==NULL || buf->base==NULL) return;
  if (rs->err != 0 || rs->eof) {
    nodec_free(buf);
    return;
  }
  if (rs->total >= rs->read_max) {
    rs->err = UV_E2BIG;
    nodec_free(buf);
    return;
  }

  // expand buffers array if needed
  if (rs->read_bufs_size <= rs->read_bufs_count) {
    size_t    newsize = (rs->read_bufs_size == 0 ? 2 : 2 * rs->read_bufs_size);
    uv_buf_t* newbufs = nodec_realloc(rs->read_bufs, newsize * sizeof(uv_buf_t));
    if (newbufs == NULL) {
      rs->err = UV_ENOMEM;
      nodec_free(buf);
      return;
    }
    rs->read_bufs = newbufs;
    rs->read_bufs_size = newsize;
  }
  // make sure the buffer length is the `nread`
  assert(buf->len == nread);
  // insert in buffers array
  rs->read_bufs[rs->read_bufs_count] = *buf;
  rs->read_bufs[rs->read_bufs_count].len = nread;  // paranoia
  rs->read_bufs_count++;
  rs->available += nread;
  rs->total += nread;
  return;
}

static void read_stream_try_resume(read_stream_t* rs) {
  assert(rs != NULL);
  uv_req_t* req = rs->req;
  if (req == NULL) return;
  async_req_resume(req, rs->err);
}

static void read_stream_freereq(read_stream_t* rs) {
  if (rs != NULL && rs->req != NULL) {
    nodec_req_free(rs->req);  // on explicit cancelation, don't immediately free
    rs->req = NULL;
  }
}

static bool async_read_stream_await(read_stream_t* rs) {
  if (rs == NULL) return true;
  if (rs->available == 0 && rs->err == 0 && !rs->eof) {
    // otherwise await an event
    if (rs->req != NULL) lh_throw_str(UV_EINVAL, "only one strand can await a read stream");
    uv_req_t* req = nodec_zalloc(uv_req_t);
    rs->req = req;
    {defer(read_stream_freereq, lh_value_ptr(rs)) {
      async_await(req);
    }}
  }
  if (rs->available > 0) return false;
  check_uv_err(rs->err);
  if (rs->eof) return true;
  assert(false); 
  return false;
}

static void _read_stream_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  if (handle == NULL) return;
  read_stream_t* rs = (read_stream_t*)handle->data;
  if (rs == NULL) return;
  size_t len = (rs->read_chunk > 0 ? rs->read_chunk : suggested_size);
  buf->base = malloc(len+1);  // always allow a zero at the end
  if (buf->base != NULL) buf->len = len;
}

static void _read_stream_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  if (nread == 0) {
    // EAGAIN or EWOULDBLOCK, but not EOF
    // Todo: do nothing? or resume?
    return;
  }
  
  read_stream_t* rs = (read_stream_t*)stream->data;
  if (rs == NULL) {
    // already stopped
    if (buf != NULL && buf->base != NULL) nodec_free(buf->base);
    return;
  }

  if (nread > 0) {
    // data available
    read_stream_push(rs,buf,(size_t)nread);      
    read_stream_try_resume(rs);
  }
  else if (nread < 0) {
    // done reading (error or UV_EOF)
    rs->err = (uverr)(nread == UV_EOF ? 0 : nread);
    if (nread == UV_EOF) rs->eof = true;
    uv_read_stop(stream);       // no more reading
    read_stream_try_resume(rs);
  }
}

read_stream_t* async_read(uv_stream_t* stream, size_t max, size_t chunk) {
  assert(stream->data == NULL); // can start reading only once!
  if (stream->data != NULL) return (read_stream_t*)stream->data;
  read_stream_t* rs = nodec_zalloc(read_stream_t);  // owned by stream
  rs->read_max = (max > 0 ? max : 1024 * 1024 * 1024);  // 1Gb by default
  rs->read_chunk = chunk;
  stream->data = rs;
  check_uv_err(uv_read_start(stream, &_read_stream_alloc_cb, &_read_stream_cb));
}

// Read into a pre-allocated buffer, or allocated on demand, and
// return the bytes read, or 0 on end-of-file.
size_t async_read_buf(read_stream_t* rs, uv_buf_t* buf ) {
  bool eof = async_read_stream_await(rs);
  if (eof) return 0;

  assert(rs->available > 0 && rs->read_bufs_count > 0);
  size_t   nread = 0;
  uv_buf_t src = rs->read_bufs[0];
  if (buf->base == NULL) {
    // copy the first buf to the result
    nread = src.len;
    *buf = src;
    rs->read_bufs[0].base = NULL;
  }
  else  {
    // user allocated buffer
    nread = (src.len > buf->len ? buf->len : src.len); //min
    memcpy(buf->base, src.base, nread );
    if (src.len > buf->len) {
      size_t todo = src.len - buf->len;
      memmove(src.base, src.base + buf->len, todo);
      src.len = todo;
      rs->read_bufs[0] = src;
    }
    else {
      nodec_free(rs->read_bufs[0].base);
      rs->read_bufs[0].base = NULL;
    }
  }
  // fix up bufs
  // todo: should maintain a head element so no memmove is needed?
  if (rs->read_bufs[0].base == NULL) {
    for (size_t i = 1; i < rs->read_bufs_count; i++) {
      rs->read_bufs[i - 1] = rs->read_bufs[i];
    }
    rs->read_bufs_count--;
  }
  assert(nread > rs->available);
  rs->available -= nread;
  return nread;
}

// Read into a preallocated buffer until full, or until eof
size_t async_read_buf_full(read_stream_t* rs, uv_buf_t buf) {
  size_t nread = 0;
  do {
    nread = async_read_buf(rs, &buf);
    assert(nread <= buf.len);
    buf.base += nread;
    buf.len -= nread;
  } while (nread > 0 && buf.len > 0);
}

// Return a single buffer that contains the entire stream contents
uv_buf_t async_read_full(read_stream_t* rs) {
  while (!async_read_stream_await(rs)) {
    // wait until eof
  }
  uv_buf_t buf = nodec_buf(NULL,0); 
  size_t nread = 0;
  if (rs->read_bufs_count == 1) {
    nread = async_read_buf(rs, &buf);
  }
  else {
    buf.base = nodec_malloc(rs->available + 1);
    buf.len = rs->available;
    nread = (buf.len==0 ? 0 : async_read_buf_full(rs, buf));
  }
  return buf;
}

// Return the entire stream as a string
char* async_read_str_full(read_stream_t* rs) {
  uv_buf_t buf = async_read_full(rs);
  if (buf.base == NULL) return NULL;
  buf.base[buf.len] = 0;
  return (char*)buf.base;
}

// Return available data as a string
char* async_read_str(read_stream_t* rs) {
  uv_buf_t buf = nodec_buf(NULL, 0);
  size_t nread = async_read_buf(rs, &buf);
  if (buf.base == NULL || nread == 0) return NULL;
  buf.base[buf.len] = 0;
  return (char*)buf.base;
}
*/

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
  {with_req(uv_write_t, req) {    
    // Todo: verify it is ok to have bufs on the stack or if we need to heap alloc them first for safety
    check_uv_err(uv_write(req, stream, bufs, buf_count, &async_write_resume));
    async_await_write(req);
  }}
}
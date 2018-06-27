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

#if (defined(_WIN32) || defined(_WIN64))
typedef ULONG  uv_buf_len_t;
#else
typedef size_t uv_buf_len_t;
#endif


uv_buf_t nodec_buf(const void* data, size_t len) {
  return uv_buf_init((char*)data, (uv_buf_len_t)(len));
}

uv_buf_t nodec_buf_null() {
  return nodec_buf(NULL, 0);
}

uv_buf_t nodec_buf_alloc(size_t len) {
  return nodec_buf(nodec_malloc(len + 1), len);  // always allow one more for zero termination
}




/* ----------------------------------------------------------------------------
Await shutdown
-----------------------------------------------------------------------------*/

static void async_await_shutdown(uv_shutdown_t* req, uv_stream_t* stream) {
  async_await_owned((uv_req_t*)req,stream);
}

static void async_shutdown_resume(uv_shutdown_t* req, uverr_t status) {
  async_req_resume((uv_req_t*)req, status);
}


/* ----------------------------------------------------------------------------
Await write requests
-----------------------------------------------------------------------------*/
static void async_await_write(uv_write_t* req, uv_stream_t* owner) {
  async_await_owned((uv_req_t*)req,owner);
}

static void async_write_resume(uv_write_t* req, uverr_t status) {
  async_req_resume((uv_req_t*)req, status);
}


/* ----------------------------------------------------------------------------
  Read chunks
  Reading from a stream is a bit complex as the API from libuv is not
  ideal for our situation; in particular, it relies on a callback that 
  is called repeatedly and cannot be stopped without performance drawbacks.
  (see: https://groups.google.com/forum/#!topic/libuv/rH8Gye57Ung)
  To work around this we start reading and put partial read data in 
  a list of buffers (`chunks`) and provide asynchronous functions to read
  from these chunks potentially waiting until data is read.
-----------------------------------------------------------------------------*/

typedef struct _chunk_t {
  struct _chunk_t* next;
  uv_buf_t     buf;
} chunk_t;

typedef struct _chunks_t {
  chunk_t*     first;
  chunk_t*     last;
} chunks_t;

// push a buffer on the chunks queue
static uv_errno_t chunks_push(chunks_t* chunks, const uv_buf_t buf, size_t nread) {
  chunk_t* chunk = nodecx_alloc(chunk_t);
  if (chunk == NULL) return UV_ENOMEM;
  // initalize
  chunk->next = NULL;
  chunk->buf = buf;
  // link it 
  if (chunks->last != NULL) {
    chunks->last->next = chunk;
  }
  else {
    chunks->first = chunk;
  }
  chunks->last = chunk;
  // ensure buf len is correct
  assert(nread <= chunk->buf.len);
  if (nread < chunk->buf.len) {
    if (chunk->buf.len > 64 && (nread / 4) * 5 <= chunk->buf.len) {
      // more than 64bytes and more than 20% wasted; we reallocate if possible
      void* newbase = nodecx_realloc(chunk->buf.base, nread + 1);
      if (newbase != NULL) chunk->buf.base = newbase;
      chunk->buf.len = (uv_buf_len_t)nread;
    }
    else {
      // just adjust the length and waste some space
      chunk->buf.len = (uv_buf_len_t)nread;
    }
  }
  return 0;
}

// Free all memory for a chunks queue
static void chunks_free(chunks_t* chunks) {
  chunk_t* chunk = chunks->first; 
  while(chunk != NULL) {
    nodec_free(chunk->buf.base);
    chunk_t* next = chunk->next;
    nodec_free(chunk);
    chunk = next;
  }
  chunks->first = chunks->last = NULL;
}

// Read one chunk into a pre-allocated buffer, or move it in place.
static size_t chunks_read_buf(chunks_t* chunks, uv_buf_t* buf) {
  if (chunks->first == NULL) return 0;
  size_t nread = 0;
  chunk_t* chunk = chunks->first;
  if (buf->base != NULL && buf->len < chunk->buf.len) {
    // pre-allocated buffer that is too small for the first chunk
    nread = buf->len;
    size_t todo = chunk->buf.len - nread;
    memcpy(buf->base, chunk->buf.base, nread);
    memmove(chunk->buf.base, chunk->buf.base + nread, todo);
    chunk->buf.len = (uv_buf_len_t)todo;
  }
  else {
    uv_buf_t src = chunk->buf;
    nread = src.len;
    // unlink the first chunk
    chunks->first = chunk->next;
    if (chunks->first == NULL) chunks->last = NULL;
    nodec_free(chunk);
    if (buf->base == NULL) {
      // not pre-allocated, just return as is :-)
      *buf = src;
    }
    else {
      // pre-allocated with enough space
      memcpy(buf->base, src.base, nread);
      nodec_free(src.base);
    }
  }
  return nread;
}

static size_t chunks_find_eol(chunks_t* chunks) {
  size_t toread = 0;
  for (chunk_t* chunk = chunks->first; chunk != NULL; chunk = chunk->next) {    
    size_t idx;
    for (idx = 0; idx < chunk->buf.len; idx++) {
      if (chunk->buf.base[idx] == '\n') {
        toread += idx + 1;
        return toread;
      }
    }
    toread += chunk->buf.len;
  }
  return 0;
}

/* ----------------------------------------------------------------------------
  Read streams
  These are returned from `async_read_start` and can the be used to read from.
-----------------------------------------------------------------------------*/

typedef struct _read_stream_t {
  uv_stream_t* stream;        // backlink (stream->data == this)
  chunks_t     chunks;        // the currently read buffers
  size_t       read_max;      // maximum bytes we are going to read
  size_t       alloc_size;    // current chunk allocation size (<= alloc_max), doubled on every new read
  size_t       alloc_max;     // maximal chunk allocation size (usually about 64kb)
  bool         read_to_eof;   // set to true to improve perfomance for reading a stream up to eof
  uv_req_t*    req;           // request object for waiting
  volatile size_t     available;     // how much data is now available
  volatile size_t     read_total;    // total bytes read until now (available <= total)
  volatile bool       eof;           // true if end-of-file reached
  volatile uv_errno_t      err;           // !=0 on error
} read_stream_t;

static void read_stream_free(read_stream_t* rs) {
  if (rs == NULL) return;
  chunks_free(&rs->chunks);
  nodec_free(rs);
}

static void read_stream_push(read_stream_t* rs, const uv_buf_t buf, size_t nread) {
  if (nread == 0 || buf.base==NULL) return;
  if (rs->err == 0) {
    if (rs->read_total >= rs->read_max) {
      rs->err = UV_E2BIG;
    }
    else {
      rs->err = chunks_push(&rs->chunks, buf, nread);
    }
  }
  if (rs->err != 0) {
    nodec_free(buf.base);
    return;
  }
  rs->available += nread;
  rs->read_total += nread;
  if (rs->read_total >= rs->read_max) {
    rs->eof = true;
  }
  return;
}

static void read_stream_try_resume(read_stream_t* rs) {
  assert(rs != NULL);
  uv_req_t* req = rs->req;
  if (req == NULL) return;
  async_req_resume(req, rs->err);
}

static void _read_stream_try_resumev(void* rsv) {
  read_stream_try_resume((read_stream_t*)rsv);
}


static void read_stream_freereq(read_stream_t* rs) {
  if (rs != NULL && rs->req != NULL) {
    nodec_req_free(rs->req);  // on explicit cancelation, don't immediately free
    rs->req = NULL;
  }
}

static void read_stream_freereqv(lh_value rsv) {
  read_stream_freereq(lh_ptr_value(rsv));
}

static bool async_read_stream_await(read_stream_t* rs, bool wait_even_if_available) {
  if (rs == NULL) return true;
  if ((wait_even_if_available || rs->available == 0) && rs->err == 0 && !rs->eof) {
    // await an event
    if (rs->req != NULL) lh_throw_str(UV_EINVAL, "only one strand can await a read stream");
    uv_req_t* req = nodec_zero_alloc(uv_req_t);
    rs->req = req;
    {defer(read_stream_freereqv, lh_value_ptr(rs)) {
      async_await_owned(req,rs->stream);
      // fprintf(stderr,"back from await\n");
    }}
  }
  nodec_check(rs->err);
  return rs->eof;
}

static void _read_stream_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  if (handle == NULL) return;
  read_stream_t* rs = (read_stream_t*)handle->data;
  if (rs == NULL) return;
  // allocate
  size_t len = (rs->alloc_size > 0 ? rs->alloc_size : suggested_size);
  buf->base = nodecx_malloc(len+1);  // always allow a zero at the end
  if (buf->base != NULL) buf->len = (uv_buf_len_t)len;
  // increase alloc size
  if (rs->alloc_size > 0 && rs->alloc_size < rs->alloc_max) {
    size_t newsize = 2 * rs->alloc_size;
    rs->alloc_size = (newsize > rs->alloc_max || newsize < rs->alloc_size ? rs->alloc_max : newsize);
  }
}


static void _read_stream_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  read_stream_t* rs = (read_stream_t*)stream->data;  
  if (nread <= 0 || rs == NULL) {
    if (buf != NULL && buf->base != NULL) {
      nodec_free(buf->base);
      //buf->base = NULL;
    }    
  }  

  if (rs != NULL) {
    if (nread > 0) {
      // data available
      read_stream_push(rs, *buf, (size_t)nread);
      if (!rs->read_to_eof || rs->eof) {
        read_stream_try_resume(rs);          
      }
    }
    else if (nread < 0) {
      // done reading (error or UV_EOF)
      rs->err = (uv_errno_t)(nread == UV_EOF ? 0 : nread);
      if (nread == UV_EOF) rs->eof = true;
      uv_read_stop(stream);       // no more reading
      read_stream_try_resume(rs);
    }
    else {
      // E_AGAIN or E_WOULDBLOCK (but not EOF)
      // Todo: just ignore? or try resume?
    }
  }
  return;
}


read_stream_t* async_read_start(uv_stream_t* stream, size_t read_max, size_t alloc_init, size_t alloc_max) {
  assert(stream->data == NULL); // can start reading only once!
  if (stream->data != NULL) return (read_stream_t*)stream->data;
  read_stream_t* rs = nodec_zero_alloc(read_stream_t);  // owned by stream
  stream->data = rs;
  rs->stream = stream; // backlink
  rs->read_max = (read_max > 0 ? read_max : 1024 * 1024 * 1024);  // 1Gb by default
  rs->alloc_size = (alloc_init == 0 ? 1024 : alloc_init);  // start small but double at every new read
  rs->alloc_max = (alloc_max == 0 ? 64*1024 : alloc_max);
  nodec_check(uv_read_start(stream, &_read_stream_alloc_cb, &_read_stream_cb));
  return rs;
}

void async_read_restart(read_stream_t* rs) {
  nodec_check(uv_read_start(rs->stream, &_read_stream_alloc_cb, &_read_stream_cb));  
}

void nodec_read_stop(uv_stream_t* stream) {
  if (stream->data == NULL) return;
  read_stream_t* rs = stream->data;
  rs->eof = true;
  uv_read_stop(stream);
}

// Read into a pre-allocated buffer, or allocated on demand, and
// return the bytes read, or 0 on end-of-file.
static size_t read_stream_read_buf(read_stream_t* rs, uv_buf_t* buf) {
  if (rs->available == 0) {
    nodec_check(rs->err);
    return 0;
  }
  else {
    size_t nread = chunks_read_buf(&rs->chunks, buf);
    assert(nread <= rs->available);
    rs->available -= nread;
    return nread;
  }
}

// Try to read at most `max` characters from all available data
static uv_buf_t read_stream_read_n(read_stream_t* rs, size_t max) {
  if (rs->available == 0) {
    nodec_check(rs->err);
    return nodec_buf_null();
  }
  else if (rs->chunks.first->buf.len == max) {
    // just one chunk that contains all available data; just return it directly
    uv_buf_t buf = nodec_buf_null();
    size_t nread = read_stream_read_buf(rs, &buf);
    assert(buf.len == nread);
    assert(nread == max);
    return buf;
  }
  else {
    // preallocate and read into that
    uv_buf_t buf = nodec_buf_alloc(max);
    size_t   total = 0;
    size_t   nread = 0;
    do {
      uv_buf_t view = nodec_buf(buf.base + total, buf.len - total);
      nread = read_stream_read_buf(rs, &view);
      total += nread;
    } while (nread > 0);
    assert(total == buf.len);
    assert(total == max);
    buf.len = (uv_buf_len_t)total; // paranoia
    return buf;
  }
}

// read all available data
static uv_buf_t read_stream_read_available(read_stream_t* rs) {
  return read_stream_read_n(rs, rs->available);
}


// Find the first `\n` occurrence (or return 0 if not found)
static size_t read_stream_find_eol(read_stream_t* rs) {
  return chunks_find_eol(&rs->chunks);
}


// Read into a pre-allocated buffer, or allocated on demand, and
// return the bytes read, or 0 on end-of-file.
size_t async_read_buf(read_stream_t* rs, uv_buf_t* buf ) {
  async_read_stream_await(rs,false);
  return read_stream_read_buf(rs,buf);
}

// Read available data 
uv_buf_t async_read_buf_available(read_stream_t* rs) {
  async_read_stream_await(rs,false);
  return read_stream_read_available(rs);
}

// Read a line
uv_buf_t async_read_buf_line(read_stream_t* rs) {
  size_t toread = 0;
  bool eof;
  do {
    eof = async_read_stream_await(rs,true);
    toread = read_stream_find_eol(rs);
  } while (toread == 0 && !eof);
  if (toread > 0) {
    return read_stream_read_n(rs,toread);
  }
  else {
    return read_stream_read_available(rs);
  }
}

// Return a single buffer that contains the entire stream contents
uv_buf_t async_read_full(read_stream_t* rs) {
  rs->read_to_eof = true; // reduces resumes
  while (!async_read_stream_await(rs,true)) {
    // wait until eof
  }
  return read_stream_read_available(rs);
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

// Read one line
char* async_read_line(read_stream_t* rs) {
  uv_buf_t buf = async_read_buf_line(rs);
  if (buf.base == NULL) return NULL;
  buf.base[buf.len] = 0;
  return (char*)buf.base;
}




/* ----------------------------------------------------------------------------
  Handle management
-----------------------------------------------------------------------------*/

static void _close_handle_cb(uv_handle_t* h) {
  if ((h->type == UV_STREAM || h->type==UV_TCP || h->type==UV_TTY) && h->data!=NULL) {
    read_stream_free((read_stream_t*)h->data);
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
  nodec_owner_release(h);
}   

void nodec_stream_free(uv_stream_t* stream) {
  if (stream->data != NULL) {
    // read stream
    nodec_read_stop(stream);
  }
  nodec_handle_free((uv_handle_t*)stream);  
}

void nodec_stream_freev(lh_value streamv) {
  nodec_stream_free((uv_stream_t*)lh_ptr_value(streamv));
}

void async_shutdown(uv_stream_t* stream) {
  if (stream==NULL) return;
  if (stream->write_queue_size>0) {
    {with_req(uv_shutdown_t, req) {
      nodec_check(uv_shutdown(req, stream, &async_shutdown_resume));
      async_await_shutdown(req, stream);
    }}
  } 
  // nodec_stream_free(stream);
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
    nodec_check(uv_write(req, stream, bufs, buf_count, &async_write_resume));
    async_await_write(req,stream);
  }}
}
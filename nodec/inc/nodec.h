/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef __nodec_h 
#define __nodec_h

#include <libhandler.h>
#include <uv.h>


struct _channel_s;
typedef struct _channel_s channel_t;

/* ----------------------------------------------------------------------------
  Asynchronous primitives
-----------------------------------------------------------------------------*/
uv_loop_t* async_loop();
void       async_await(uv_req_t* req);

void       async_await_fs(uv_fs_t* req);
void       async_await_connect(uv_connect_t* req);
void       async_await_shutdown(uv_shutdown_t* req);


/* ----------------------------------------------------------------------------
  Asynchronous combinators
-----------------------------------------------------------------------------*/

void interleave(ssize_t n, lh_actionfun** actions);


/* ----------------------------------------------------------------------------
  File system (fs)
-----------------------------------------------------------------------------*/

int       asyncx_stat(const char* path, uv_stat_t* stat);
uv_stat_t async_stat(const char* path);
uv_stat_t async_fstat(uv_file file);
uv_file   async_fopen(const char* path, int flags, int mode);
void      async_fclose(uv_file file);
ssize_t   async_fread(uv_file file, uv_buf_t* buf, int64_t offset);

// File system convenience functions

char*     async_fread_full(const char* path);

/* ----------------------------------------------------------------------------
  Streams
-----------------------------------------------------------------------------*/
void        nodec_handle_free(uv_handle_t* handle);
void        nodec_stream_free(uv_stream_t* stream);
void        async_shutdown(uv_stream_t* stream);

ssize_t     async_read(uv_stream_t* stream, uv_buf_t buffer, ssize_t offset );
ssize_t     async_read_full(uv_stream_t* stream, uv_buf_t* buffer);

/* ----------------------------------------------------------------------------
  TCP
-----------------------------------------------------------------------------*/
typedef channel_t tcp_channel_t;

uv_tcp_t*   nodec_tcp_alloc();
void        nodec_tcp_free(uv_tcp_t* tcp);
void        nodec_tcp_freev(lh_value tcp);
void        nodec_tcp_bind(uv_tcp_t* handle, const struct sockaddr_in* addr, unsigned int flags);
tcp_channel_t*  nodec_tcp_listen(uv_tcp_t* tcp, int backlog, bool channel_owns_tcp);

// Convenience
tcp_channel_t*  nodec_tcp_listen_at4(const char* ip, int port, int backlog, unsigned int flags);

/* ----------------------------------------------------------------------------
  Other
-----------------------------------------------------------------------------*/

typedef void (nc_entryfun_t)();

void async_main( nc_entryfun_t* entry );


/* ----------------------------------------------------------------------------
  Channels
-----------------------------------------------------------------------------*/

typedef struct _channel_elem {
  lh_value data;
  lh_value arg;
  int      err;
} channel_elem;

typedef void (channel_release_elem_fun)(channel_elem elem);

channel_t* channel_alloc(ssize_t queue_max);
channel_t* channel_alloc_ex(ssize_t queue_max, lh_releasefun* release, lh_value release_arg, channel_release_elem_fun* release_elem );
void channel_free(channel_t* channel);
int  channel_emit(channel_t* channel, channel_elem elem);
channel_elem channel_receive(channel_t* channel);

void channel_freev(lh_value vchannel);
#define with_channel(name) channel_t* name = channel_alloc(-1); defer(&channel_freev,lh_value_ptr(name))

/* ----------------------------------------------------------------------------
  Safe allocation
  These raise an exception on failure
  The code:
    {with_alloc(mystruct,name){
      ...
    }}
  will safely allocate a `mystruct*` to `name` which can be used inside `...`
  and will be deallocated safely if an exception is thrown or when exiting
  the block scope.
-----------------------------------------------------------------------------*/

#if defined(_MSC_VER) && defined(_DEBUG)
// Enable debugging logs on msvc 
# undef _malloca // suppress warning
# define _CRTDBG_MAP_ALLOC
# include <crtdbg.h>
# define nodec_malloc malloc
# define nodec_calloc calloc
# define nodec_realloc realloc
# define nodec_free free
#else
# define nodec_malloc _nodec_malloc
# define nodec_calloc _nodec_calloc
# define nodec_realloc _nodec_realloc
# define nodec_free _nodec_free
#endif

void  nodec_register_malloc(lh_mallocfun* _malloc, lh_callocfun* _calloc, lh_reallocfun* _realloc, lh_freefun* _free);
void  nodec_check_memory();

void* _nodec_malloc(size_t size);
void* _nodec_calloc(size_t count, size_t size);
void* _nodec_realloc(void* p, size_t newsize);
void  _nodec_free(void* p);
void  nodec_freev(lh_value p);
char* nodec_strdup(const char* s);
char* nodec_strndup(const char* s, size_t max);

#define nodec_alloc(tp)         ((tp*)(nodec_malloc(sizeof(tp))))
#define nodec_nalloc(n,tp)      ((tp*)(nodec_malloc((n)*sizeof(tp))))
#define nodec_ncalloc(n,tp)     ((tp*)(nodec_calloc(n,sizeof(tp))))

#define with_free(name)         defer(nodec_freev,lh_value_ptr(name))
#define with_alloc(tp,name)     tp* name = nodec_alloc(tp); with_free(name)
#define with_nalloc(n,tp,name)  tp* name = nodec_nalloc(n,tp); with_free(name)
#define with_ncalloc(n,tp,name) tp* name = nodec_ncalloc(n,tp); with_free(name)

#define nodec_zero(tp,ptr)      memset(ptr,0,sizeof(tp));

#endif // __nodec_h

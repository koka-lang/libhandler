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

/* ----------------------------------------------------------------------------
  Notes:
  - Private functions are prepended with an underscore. Dont use them directly.
  - Pre- and post-fixes:
    "async_" : for asynchronous functions that might interleave
    "nodec_" : synchronous functions that might throw exeptions or use other effects.
    "nodecx_": synchronous functions that return an explicit error result.
    "..._t"  : for types
    "with_"  : for scoped combinators, to be used with double curly braces:
               i.e. "{with_alloc(tp,name){ ... <use name> ... }}"

  - an `lh_value` is used to mimic polymorphism. There are a host of 
    conversion functions, like `lh_ptr_value` (from value to pointer)
    or `lh_value_int` (from int to a value).
-----------------------------------------------------------------------------*/


// Forward declarations 
typedef struct _channel_t channel_t;

// Throw on an error
void nodec_throw(int err);
void nodec_throw_msg(int err, const char* msg);


/* ----------------------------------------------------------------------------
  Cancelation scope
-----------------------------------------------------------------------------*/

// private
implicit_declare(_cancel_scope)
lh_value _cancel_scope_alloc();

// execute under a cancelation scope
#define with_cancel_scope()        with_implicit_defer(nodec_freev,_cancel_scope_alloc(),_cancel_scope)

// Asynchronously cancel all outstanding requests under the same
// cancelation scope.
void async_scoped_cancel();

/* ----------------------------------------------------------------------------
  Asynchronous combinators
-----------------------------------------------------------------------------*/

// Interleave `n` actions with arguments `arg_results`. 
// The result of each action is stored again in `arg_results`; when
// an exception is raised, it is rethrown from `interleave` once all
// its actions have finished. Interleave introduces a cancelation 
// scope.
void interleave(size_t n, lh_actionfun* actions[], lh_value arg_results[]);

// General timeout routine over an `action`. 
lh_value async_timeout(lh_actionfun* action, lh_value arg, uint64_t timeout, bool* timedout);

// Return the value of the first returning action, canceling the other.
lh_value async_firstof(lh_actionfun* action1, lh_value arg1, lh_actionfun* action2, lh_value arg2, bool* first);

// Asynchronously wait for `timeout` milli-seconds.
void async_wait(uint64_t timeout);

// Yield asynchronously to other strands.
void async_yield();

/* ----------------------------------------------------------------------------
  File system (fs)
-----------------------------------------------------------------------------*/

uv_errno_t  asyncx_stat(const char* path, uv_stat_t* stat);
uv_stat_t   async_stat(const char* path);
uv_stat_t   async_fstat(uv_file file);
uv_file     async_fopen(const char* path, int flags, int mode);
void        async_fclose(uv_file file);
size_t      async_fread(uv_file file, uv_buf_t* buf, int64_t offset);

// ----------------------------------
// File system convenience functions

char*       async_fread_full(const char* path);

typedef lh_value(nodec_file_fun)(uv_file file, lh_value arg);
lh_value    async_with_fopen(const char* path, int flags, int mode, nodec_file_fun* action, lh_value arg);


/* ----------------------------------------------------------------------------
  Buffers are `uv_buf_t` which contain a `base` pointer and the
  available `len` bytes. These buffers are usually passed by value.
-----------------------------------------------------------------------------*/

// Initialize a libuv buffer which is a record with a data pointer and its length.
uv_buf_t nodec_buf(void* data, size_t len);

// Create a NULL buffer, i.e. `nodec_buf(NULL,0)`.
uv_buf_t nodec_buf_null();

// Create and allocate a buffer
uv_buf_t nodec_buf_alloc(size_t len);


/* ----------------------------------------------------------------------------
  Streams
-----------------------------------------------------------------------------*/

void        nodec_handle_free(uv_handle_t* handle);
void        nodec_stream_free(uv_stream_t* stream);
void        nodec_stream_freev(lh_value streamv);
void        async_shutdown(uv_stream_t* stream);
 
#define with_stream(s) \
    defer_exit(async_shutdown(s),&nodec_stream_freev,lh_value_ptr(s))

void        nodec_read_start(uv_stream_t* stream, size_t read_max, size_t alloc_init, size_t alloc_max);
void        nodec_read_stop(uv_stream_t* stream);
void        nodec_read_restart(uv_stream_t* stream);

size_t      async_read_buf(uv_stream_t* stream, uv_buf_t* buf);
uv_buf_t    async_read_buf_available(uv_stream_t* stream);
uv_buf_t    async_read_buf_line(uv_stream_t* stream);
uv_buf_t    async_read_full(uv_stream_t* stream);

char*       async_read_str(uv_stream_t* stream);
char*       async_read_str_full(uv_stream_t* stream);
char*       async_read_line(uv_stream_t* stream);

void        async_write(uv_stream_t* stream, const char* s);
void        async_write_bufs(uv_stream_t* stream, uv_buf_t bufs[], unsigned int buf_count);
void        async_write_strs(uv_stream_t* stream, const char* strings[], unsigned int string_count );
void        async_write_data(uv_stream_t* stream, const void* data, size_t len);
void        async_write_buf(uv_stream_t* stream, uv_buf_t buf);


/* ----------------------------------------------------------------------------
  IP4 and IP6 Addresses
-----------------------------------------------------------------------------*/

void nodec_ip4_addr(const char* ip, int port, struct sockaddr_in* addr);
void nodec_ip6_addr(const char* ip, int port, struct sockaddr_in6* addr);

#define define_ip4_addr(ip,port,name)  \
  struct sockaddr_in name##_ip4; nodec_ip4_addr(ip,port,&name##_ip4); \
  struct sockaddr* name = (struct sockaddr*)&name##_ip4;

#define define_ip6_addr(ip,port,name)  \
  struct sockaddr_in name##_ip6; nodec_ip6_addr(ip,port,&name##_ip6); \
  struct sockaddr* name = (struct sockaddr*)&name##_ip6;



/* ----------------------------------------------------------------------------
  TCP
-----------------------------------------------------------------------------*/
typedef struct _channel_t tcp_channel_t;
void            channel_freev(lh_value vchannel);
#define with_tcp_channel(ch)  defer(channel_freev,lh_value_ptr(ch))

uv_tcp_t*       nodec_tcp_alloc();
void            nodec_tcp_free(uv_tcp_t* tcp);
void            nodec_tcp_freev(lh_value tcp);

void            nodec_tcp_bind(uv_tcp_t* handle, const struct sockaddr* addr, unsigned int flags);
tcp_channel_t*  nodec_tcp_listen(uv_tcp_t* tcp, int backlog, bool channel_owns_tcp);
uv_stream_t*    async_tcp_channel_receive(tcp_channel_t* ch);

// Convenience:

typedef void    (nodec_tcp_servefun)(int id, uv_stream_t* client);

void async_tcp_server_at(const struct sockaddr* addr, int backlog, int max_interleaving, 
                          uint64_t timeout, nodec_tcp_servefun* servefun, lh_actionfun* on_exn);


/* ----------------------------------------------------------------------------
  HTTP
-----------------------------------------------------------------------------*/
typedef int http_status;

void throw_http_err(http_status status);
void throw_http_err_str(http_status status, const char* msg);
void throw_http_err_strdup(http_status status, const char* msg);

void async_http_server_at(const struct sockaddr* addr, int backlog, int max_interleaving, 
                          uint64_t timeout, nodec_tcp_servefun* servefun);



/* ----------------------------------------------------------------------------
  TTY
-----------------------------------------------------------------------------*/

lh_value _nodec_tty_allocv();
void     _nodec_tty_freev(lh_value ttyv);

implicit_declare(tty)

#define with_tty()  \
    with_implicit_defer_exit(async_tty_shutdown(),_nodec_tty_freev,_nodec_tty_allocv(),tty)

char* async_tty_readline();
void  async_tty_write(const char* s);
void  async_tty_shutdown();


/* ----------------------------------------------------------------------------
  Main entry point
-----------------------------------------------------------------------------*/

typedef void (nodec_main_fun_t)();

uv_errno_t  async_main( nodec_main_fun_t* entry );




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
# define nodecx_malloc  malloc
# define nodecx_calloc  calloc
# define nodecx_realloc realloc
# define nodecx_free    free
# define nodec_malloc(sz)     (check_nonnull(malloc(sz)))
# define nodec_calloc(n,sz)   (check_nonnull(calloc(n,sz)))
# define nodec_realloc(p,sz)  (check_nonnull(realloc(p,sz)))
# define nodec_free           _nodec_free
#else
# define nodecx_malloc  _nodecx_malloc
# define nodecx_calloc  _nodecx_calloc
# define nodecx_realloc _nodecx_realloc
# define nodecx_free    free
# define nodec_malloc  _nodec_malloc
# define nodec_calloc  _nodec_calloc
# define nodec_realloc _nodec_realloc
# define nodec_free    _nodec_free
#endif

void  nodec_register_malloc(lh_mallocfun* _malloc, lh_callocfun* _calloc, lh_reallocfun* _realloc, lh_freefun* _free);
void  nodec_check_memory();
void* check_nonnull(void* p);   // throws on a non-null pointer

void* _nodecx_malloc(size_t size);
void* _nodecx_calloc(size_t count, size_t size);
void* _nodecx_realloc(void* p, size_t newsize);
void* _nodec_malloc(size_t size);
void* _nodec_calloc(size_t count, size_t size);
void* _nodec_realloc(void* p, size_t newsize);
void  _nodec_free(const void* p);

void  nodec_freev(lh_value p);
char* nodec_strdup(const char* s);
char* nodec_strndup(const char* s, size_t max);

#define nodecx_alloc(tp)          ((tp*)(nodecx_malloc(sizeof(tp))))
#define nodecx_zero_alloc(tp)     ((tp*)(nodecx_calloc(1,sizeof(tp))))

#define nodec_alloc(tp)           ((tp*)(nodec_malloc(sizeof(tp))))
#define nodec_alloc_n(n,tp)       ((tp*)(nodec_malloc((n)*sizeof(tp))))
#define nodec_zero_alloc_n(n,tp)  ((tp*)(nodec_calloc(n,sizeof(tp))))
#define nodec_zero_alloc(tp)      nodec_zero_alloc_n(1,tp)
#define nodec_realloc_n(p,n,tp)   ((tp*)(nodec_realloc(p,(n)*sizeof(tp))))

#define with_free(name)               defer(nodec_freev,lh_value_ptr(name))
#define with_alloc(tp,name)           tp* name = nodec_alloc(tp); with_free(name)
#define with_alloc_n(n,tp,name)       tp* name = nodec_alloc_n(n,tp); with_free(name)
#define with_zero_alloc_n(n,tp,name)  tp* name = nodec_zero_alloc_n(n,tp); with_free(name)
#define with_zero_alloc(tp,name)      with_zero_alloc_n(1,tp,name)

#define nodec_zero(tp,ptr)        memset(ptr,0,sizeof(tp));



#endif // __nodec_h

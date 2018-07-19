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
#include <http_parser.h>

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

typedef uv_fs_t nodec_scandir_t;
void        nodec_scandir_free(nodec_scandir_t* scanreq);
void        nodec_scandir_freev(lh_value scanreqv);
#define with_scandir(req)  defer(nodec_scandir_freev,lh_value_ptr(req))

nodec_scandir_t* async_scandir(const char* path);
bool async_scandir_next(nodec_scandir_t* scanreq, uv_dirent_t* dirent);

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
uv_buf_t nodec_buf(const void* data, size_t len);
uv_buf_t nodec_buf_str(const char* s);

// Create a NULL buffer, i.e. `nodec_buf(NULL,0)`.
uv_buf_t nodec_buf_null();

// Create and allocate a buffer
uv_buf_t nodec_buf_alloc(size_t len);
uv_buf_t nodec_buf_realloc(uv_buf_t buf, size_t len);

void nodec_buf_ensure(uv_buf_t* buf, size_t needed);
void nodec_buf_ensure_ex(uv_buf_t* buf, size_t needed, size_t initial_size, size_t max_increase);

bool nodec_buf_is_null(uv_buf_t buf);
void nodec_buf_free(uv_buf_t buf);
void nodec_bufref_free(uv_buf_t* buf);
void nodec_bufref_freev(lh_value bufref);

#define with_buf(buf)                 uv_buf_t buf = nodec_buf_null(); defer(nodec_bufref_freev,lh_value_any_ptr(&buf))
#define with_on_abort_free_buf(buf)   uv_buf_t buf = nodec_buf_null(); on_abort(nodec_bufref_freev,lh_value_any_ptr(&buf))

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
void        nodec_set_read_max(uv_stream_t* stream, size_t read_max);

uv_buf_t    async_read_buf(uv_stream_t* stream);
uv_buf_t    async_read_buf_available(uv_stream_t* stream);
uv_buf_t    async_read_buf_line(uv_stream_t* stream);
uv_buf_t    async_read_buf_all(uv_stream_t* stream);
size_t      async_read_into_all(uv_stream_t* stream, uv_buf_t buf, bool* at_eof);
uv_buf_t    async_read_buf_including(uv_stream_t* stream, size_t* idx, const void* pat, size_t pat_len);

char*       async_read(uv_stream_t* stream);
char*       async_read_all(uv_stream_t* stream);
char*       async_read_line(uv_stream_t* stream);


void        async_write(uv_stream_t* stream, const char* s);
void        async_write_bufs(uv_stream_t* stream, uv_buf_t bufs[], size_t buf_count);
void        async_write_strs(uv_stream_t* stream, const char* strings[], size_t string_count );
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

void nodec_sockname(const struct sockaddr* addr, char* buf, size_t bufsize);

void async_getnameinfo(const struct sockaddr* addr, int flags, char** node, char** service);
struct addrinfo* async_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints);

void nodec_free_addrinfo(struct addrinfo* info);
void nodec_free_addrinfov(lh_value infov);
#define with_addrinfo(name)  defer(nodec_free_addrinfov,lh_value_ptr(name))


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

typedef void    (nodec_tcp_servefun)(int id, uv_stream_t* client, lh_value arg);

void async_tcp_server_at(const struct sockaddr* addr, int backlog, int max_interleaving, 
                          uint64_t timeout, 
                          nodec_tcp_servefun* servefun, lh_actionfun* on_exn,
                          lh_value arg);

uv_stream_t* async_tcp_connect_at(const struct sockaddr* addr);
uv_stream_t* async_tcp_connect(const char* host, const char* service /*NULL="http"*/);


/* ----------------------------------------------------------------------------
  HTTP
-----------------------------------------------------------------------------*/
typedef enum http_status http_status_t;
typedef enum http_method http_method_t;

typedef struct _http_in_t http_in_t;
typedef struct _http_out_t http_out_t;

void throw_http_err(http_status_t status);
void throw_http_err_str(http_status_t status, const char* msg);
void throw_http_err_strdup(http_status_t status, const char* msg);
const char* nodec_http_status_str(http_status_t code);
const char* nodec_http_method_str(http_method_t method);


typedef void (nodec_http_servefun)(int strand_id, http_in_t* in, http_out_t* out, lh_value arg);

void async_http_server_at(const struct sockaddr* addr, int backlog, int max_interleaving, 
                          uint64_t timeout, nodec_http_servefun* servefun, lh_value arg);


typedef lh_value (http_connect_fun)(http_in_t* in, http_out_t* out, lh_value arg);

lh_value async_http_connect(const char* url, http_connect_fun* connectfun, lh_value arg);


void   http_in_clear(http_in_t* in);
void   http_in_clearv(lh_value inv);
size_t async_http_in_init(http_in_t* in, uv_stream_t* stream, bool is_request );

const char*   http_in_url(http_in_t* in);
http_status_t   http_in_status(http_in_t* in);
http_method_t http_in_method(http_in_t* in);
uint64_t      http_in_content_length(http_in_t* in);
const char*   http_in_header(http_in_t* in, const char* name);
const char*   http_in_header_next(http_in_t* in, const char** value, size_t* iter);

// use this on connections to wait for a response
size_t async_http_in_read_headers(http_in_t* in);

// Read body in parts; returned buffer is only valid until the next read
// Returns a null buffer when the end is reached.
uv_buf_t      async_http_in_read_body_buf(http_in_t* in);

// Read the full body; the returned buf should be deallocated by the caller.
// Pass `initial_size` 0 to automatically use the content-length or initially
// received buffer size.
uv_buf_t      async_http_in_read_body(http_in_t* in, size_t initial_size);


/*-----------------------------------------------------------------

-----------------------------------------------------------------*/


void http_out_init(http_out_t* out, uv_stream_t* stream);
void http_out_init_server(http_out_t* out, uv_stream_t* stream, const char* server_name);
void http_out_init_client(http_out_t* out, uv_stream_t* stream, const char* host_name);

void http_out_clear(http_out_t* out);
void http_out_clearv(lh_value respv);

#define with_http_out(stream,out)  http_out_t _resp; http_out_t* out = &_resp; http_out_init(out,stream); defer(http_out_clearv,lh_value_any_ptr(out))

// Add headers
void http_out_add_header(http_out_t* out, const char* field, const char* value);

// Send the headers
void http_out_send_headers(http_out_t* out, const char* prefix, const char* postfix);
void http_out_send_status_headers(http_out_t* out, http_status_t status, bool end);
void http_out_send_request_headers(http_out_t* out, http_method_t method, const char* url, bool end);

// Send full body at once
void http_out_send_body_bufs(http_out_t* out, uv_buf_t bufs[], size_t count);
void http_out_send_body_buf(http_out_t* out, uv_buf_t buf);
void http_out_send_body(http_out_t* out, const char* s);

// Send chunked up body
void http_out_send_chunked_start(http_out_t* out);
void http_out_send_chunk_bufs(http_out_t* out, uv_buf_t bufs[], size_t count);
void http_out_send_chunk_buf(http_out_t* out, uv_buf_t buf);
void http_out_send_chunk(http_out_t* out, const char* s);
void http_out_send_chunked_end(http_out_t* out);

/* ----------------------------------------------------------------------------
   URL's
-----------------------------------------------------------------------------*/
typedef struct _nodec_url_t nodec_url_t;

void nodec_url_free(nodec_url_t* url);
void nodec_url_freev(lh_value urlv);

nodec_url_t*  nodecx_parse_url(const char* url, bool hostonly);
nodec_url_t*  nodec_parse_url(const char* url, bool hostonly);

#define with_url(urlstr,url)  nodec_url_t* url = nodec_parse_url(urlstr,false); defer(nodec_url_freev,lh_value_ptr(url))
#define withx_url(urlstr,url)  nodec_url_t* url = nodec_parsex_url(urlstr,false); defer(nodec_url_freev,lh_value_ptr(url))

#define with_host_url(urlstr,url)  nodec_url_t* url = nodec_parse_url(urlstr,true); defer(nodec_url_freev,lh_value_ptr(url))
#define withx_host_url(urlstr,url)  nodec_url_t* url = nodec_parsex_url(urlstr,true); defer(nodec_url_freev,lh_value_ptr(url))

const char* nodec_url_schema(const nodec_url_t* url);
const char* nodec_url_host(const nodec_url_t* url);
const char* nodec_url_path(const nodec_url_t* url);
const char* nodec_url_query(const nodec_url_t* url);
const char* nodec_url_fragment(const nodec_url_t* url);
const char* nodec_url_userinfo(const nodec_url_t* url);
uint16_t nodec_url_port(const nodec_url_t* url);



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
const void* nodec_memmem(const void* src, size_t src_len, const void* pat, size_t pat_len);

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

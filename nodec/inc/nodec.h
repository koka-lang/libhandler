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
  Asynchronous primitives
-----------------------------------------------------------------------------*/
uv_loop_t* async_loop();
int        async_await(uv_req_t* req);
int        async_await_fs(uv_fs_t* req);

// ---------------------------------------------------------------------------------
// Private: todo: is there a better way to define internally only visible routines?

const lh_handlerdef _local_async_hdef;
void                _local_async_resume_request(lh_resume r, lh_value local, uv_req_t* req, int err);

void       _async_fs_cb(uv_fs_t* req);
void       _async_plain_cb(uv_req_t* uvreq, int err);

void       _check_uv_err(int err);
void       _check_uv_errmsg(int err, const char* msg);


/* ----------------------------------------------------------------------------
  Asynchronous combinators
-----------------------------------------------------------------------------*/

void interleave(ssize_t n, lh_actionfun** actions);


/* ----------------------------------------------------------------------------
  File system (fs)
-----------------------------------------------------------------------------*/

uv_stat_t async_stat(const char* path);
uv_stat_t async_fstat(uv_file file);
uv_file   async_fopen(const char* path, int flags, int mode);
void      async_fclose(uv_file file);
ssize_t   async_fread(uv_file file, uv_buf_t* buf, int64_t offset);

// File system convenience functions

char*     async_fread_full(const char* path);

/* ----------------------------------------------------------------------------
  Other
-----------------------------------------------------------------------------*/

typedef void (nc_entryfun_t)();

void async_main( nc_entryfun_t* entry );


#endif // __nodec_h

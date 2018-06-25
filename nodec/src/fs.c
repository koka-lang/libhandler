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
#include <fcntl.h>



/*-----------------------------------------------------------------
  handling file system requests
-----------------------------------------------------------------*/

// Await a file system request
uverr asyncx_await_fs(uv_fs_t* req) {
  return asyncx_await((uv_req_t*)req,NULL);
}

void async_await_fs(uv_fs_t* req) {
  async_await((uv_req_t*)req);
}

// The entry point for filesystem callbacks
static void async_fs_resume(uv_fs_t* uvreq) {
  if (uvreq == NULL) return;
  uverr err = (uvreq->result >= 0 ? 0 : (uverr)uvreq->result);
  async_req_resume((uv_req_t*)uvreq, err);
}


#define with_fs_req(req,loop)  uv_loop_t* loop = async_loop(); \
                               with_unowned_req(uv_fs_t,req)



void async_await_file(uv_fs_t* req, uv_file owner) {
  async_await_owned((uv_req_t*)req, (void*)((intptr_t)owner));
}

#define with_file_req(req,loop)  with_fs_req(req,loop)




/*-----------------------------------------------------------------
  Async wrappers
-----------------------------------------------------------------*/

uverr asyncx_stat(const char* path, uv_stat_t* stat ) {
  nodec_zero(uv_stat_t,stat);
  uverr err = 0;
  {with_fs_req(req, loop){
    check_uverr(uv_fs_stat(loop, req, path, &async_fs_resume));
    err = asyncx_await_fs(req);
    if (err == 0) *stat = req->statbuf;
  }}
  return err; 
}

uv_stat_t async_stat(const char* path) {
  uv_stat_t stat;
  check_uverr_msg(asyncx_stat(path, &stat), path);
  return stat;
}

uv_stat_t async_fstat(uv_file file) {
  uv_stat_t stat; 
  nodec_zero(uv_stat_t, &stat);
  {with_fs_req(req, loop) {
    check_uverr(uv_fs_fstat(loop, req, file, &async_fs_resume));
    async_await_fs(req);
    stat = req->statbuf;
  }}  
  return stat;
}


uverr asyncx_fopen(const char* path, int flags, int mode, uv_file* file) {
  *file = -1;
  uverr err = 0;
  {with_fs_req(req, loop) {
    check_uverr_msg(uv_fs_open(loop, req, path, flags, mode, &async_fs_resume), path);
    err = asyncx_await_fs(req);
    if (err == 0) *file = (uv_file)(req->result);
  }}
  return err;
}

uv_file async_fopen(const char* path, int flags, int mode) {
  uv_file file = -1;
  check_uverr_msg(asyncx_fopen(path, flags, mode, &file), path);
  return file;
}

void nodec_fclose(uv_file file) {
  nodec_owner_release((void*)((intptr_t)file));
}

void nodec_fclosev(lh_value filev) {
  nodec_fclose((uv_file)lh_int_value(filev));
}

void async_fclose(uv_file file) {
  if (file < 0) return;
  {defer(nodec_fclosev, lh_value_int(file)) {
    {with_file_req(req, loop) {
      check_uverr(uv_fs_close(loop, req, file, &async_fs_resume));
      async_await_file(req, file);
    }}
  }}
}

size_t async_fread(uv_file file, uv_buf_t* buf, int64_t file_offset) {
  size_t read = 0;
  {with_file_req(req, loop) {
    check_uverr(uv_fs_read(loop, req, file, buf, 1, file_offset, &async_fs_resume));
    async_await_file(req, file);
    read = (size_t)req->result;
  }}
  return read;
}

static void async_file_closev(lh_value vfile) {
  uv_file file = lh_int_value(vfile);
  if (file >= 0) async_fclose(file);
}

typedef struct __fopen_args {
  lh_value arg;
  uv_file  file;
  nodec_file_fun* action;
} _fopen_args;

static lh_value _fopen_action(lh_value argsv) {
  _fopen_args* args = (_fopen_args*)lh_ptr_value(argsv);
  return args->action(args->file, args->arg);
}

lh_value async_with_fopen(const char* path, int flags, int mode, nodec_file_fun* action, lh_value arg ) {
  _fopen_args args;
  args.arg = arg;
  args.action = action;
  args.file = async_fopen(path, flags, 0);
  return lh_finally(
    &_fopen_action, lh_value_any_ptr(&args),
    &async_file_closev, lh_value_int(args.file)
  );
}

lh_value _async_fread_full(uv_file file, lh_value _arg) {
  uv_stat_t stat = async_fstat(file);
  if (stat.st_size >= MAXSIZE_T) lh_throw_errno(E2BIG);
  size_t    size = (size_t)stat.st_size;
  char*     buffer = nodec_alloc_n(size + 1, char);
  {on_abort(nodec_freev, lh_value_ptr(buffer)) {
    uv_buf_t buf = nodec_buf(buffer, size);
    size_t read = 0;
    size_t total = 0;
    while (total < size) {
      size_t read = async_fread(file, &buf, -1);
      if (read == 0) break;
      total += read;
      if (total > size) {
        total = size;
      }
      else {
        buf = nodec_buf(buffer + total, size - total);
      }
    }
    buffer[total] = 0;
  }}
  return lh_value_ptr(buffer);
}


char* async_fread_full(const char* path) {
  lh_value result = async_with_fopen(path, O_RDONLY, 0, &_async_fread_full, lh_value_null);  
  return (char*)lh_ptr_value(result);
}

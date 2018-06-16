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

static uv_fs_t* uv_fs_alloc() {
  return (uv_fs_t*)calloc(1,sizeof(uv_fs_t));
}

static void uv_fs_free(uv_fs_t* req) {
  if (req != NULL) {
    uv_fs_req_cleanup(req);
    free(req);
  }
}

static void uv_fs_freev(lh_value req) {
  uv_fs_free((uv_fs_t*)lh_ptr_value(req));
}

#define with_fs_req(req,loop)  uv_fs_t*   req = uv_fs_alloc(); \
                               uv_loop_t* loop = async_loop(); \
                               defer(uv_fs_freev,lh_value_ptr(req))


/*-----------------------------------------------------------------
  Async wrappers
-----------------------------------------------------------------*/

int asyncx_stat(const char* path, uv_stat_t* stat ) {
  nodec_zero(uv_stat_t,stat);
  int err = 0;
  {with_fs_req(req, loop){
    check_uv_err(uv_fs_stat(loop, req, path, &_async_fs_cb));
    err = asyncx_await_fs(req);
    if (err == 0) *stat = req->statbuf;
  }}
  return err; 
}

uv_stat_t async_stat(const char* path) {
  uv_stat_t stat;
  check_uv_errmsg(asyncx_stat(path, &stat), path);
  return stat;
}

uv_stat_t async_fstat(uv_file file) {
  uv_stat_t stat; 
  nodec_zero(uv_stat_t, &stat);
  {with_fs_req(req, loop) {
    check_uv_err(uv_fs_fstat(loop, req, file, &_async_fs_cb));
    async_await_fs(req);
    stat = req->statbuf;
  }}  
  return stat;
}


int asyncx_fopen(const char* path, int flags, int mode, uv_file* file) {
  *file = -1;
  int err = 0;
  {with_fs_req(req, loop) {
    check_uv_errmsg(uv_fs_open(loop, req, path, flags, mode, &_async_fs_cb), path);
    err = asyncx_await_fs(req);
    if (err == 0) *file = (uv_file)(req->result);
  }}
  return err;
}

uv_file async_fopen(const char* path, int flags, int mode) {
  uv_file file = -1;
  check_uv_errmsg(asyncx_fopen(path, flags, mode, &file), path);
  return file;
}

void async_fclose(uv_file file) {
  if (file < 0) return;
  {with_fs_req(req, loop) {
    check_uv_err(uv_fs_close(loop, req, file, &_async_fs_cb));
    async_await_fs(req);
  }}
}

ssize_t async_fread(uv_file file, uv_buf_t* buf, int64_t offset) {
  ssize_t read = 0;
  {with_fs_req(req, loop) {
    check_uv_err(uv_fs_read(loop, req, file, buf, 1, offset, &_async_fs_cb));
    async_await_fs(req);
    read = req->result;
  }}
  return read;
}

void uv_file_close(lh_value vfile) {
  uv_file file = lh_int_value(vfile);
  if (file >= 0) async_fclose(file);
}

char* async_fread_full(const char* path) {
  char*   buffer = NULL;
  uv_file file = async_fopen(path, O_RDONLY, 0);
  {defer(uv_file_close, file) {
    uv_stat_t stat = async_fstat(file);
    ssize_t   size = stat.st_size;
    buffer = nodec_nalloc(size + 1,char);
    {on_exn(nodec_freev, lh_value_ptr(buffer)) {
      uv_buf_t buf = uv_buf_init(buffer, (unsigned)size);
      ssize_t read = 0;
      ssize_t total = 0;
      while (total < size){
        ssize_t read = async_fread(file, &buf, -1);
        if (read == 0) break;
        total += read;
        if (total > size) {
          total = size;
        }
        else {
          // Todo: is this how we should use these buffers??
          buf = uv_buf_init(buffer + total, (unsigned)(size - total));
        }
      }
      buffer[total] = 0;
    }}
  }}
  return buffer;
}

/*
static int async_fread_fullx(const char* path, ssize_t* len, char** contents) {
  *contents = NULL;
  *len = 0;
  uv_file file;
  int err = async_fopenx(path, O_RDONLY, 0, &file);
  if (err != 0) return err;
  uv_stat_t stat = async_fstat(file);
  if (err == 0) {
    ssize_t size = stat.st_size;
    char*   buffer = (char*)malloc(size + 1);
    uv_buf_t buf = uv_buf_init(buffer, (unsigned)size);
    ssize_t read = 0;
    ssize_t total = 0;
    while (total < size && (err = async_freadx(file, &buf, -1, &read)) == 0 && read > 0) {
      total += read;
      if (total > size) {
        total = size;
      }
      else {
        buf = uv_buf_init(buffer + total, (unsigned)(size - total));
      }
    }
    buffer[total] = 0;
    if (err == 0) {
      *contents = buffer;
      *len = total;
    }
    else {
      free(buffer);
    }
  }
  async_fclosex(file);
  return err;
}

char* async_fread_full(const char* path) {
  char*   contents = NULL;
  ssize_t len = 0;
  check_uv_errmsg(async_fread_fullx(path, &len, &contents), path);
  return contents;
}
*/


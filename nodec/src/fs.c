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
#include <fcntl.h>



/*-----------------------------------------------------------------
  handling file system requests
-----------------------------------------------------------------*/

// Await a file system request
uv_errno_t asyncx_await_fs(uv_fs_t* req) {
  return asyncx_await((uv_req_t*)req,0,NULL);
}

void async_await_fs(uv_fs_t* req) {
  async_await_once((uv_req_t*)req);
}

// The entry point for filesystem callbacks
static void async_fs_resume(uv_fs_t* uvreq) {
  if (uvreq == NULL) return;
  uv_errno_t err = (uvreq->result >= 0 ? 0 : (uv_errno_t)uvreq->result);
  async_req_resume((uv_req_t*)uvreq, err);
}


#define with_fs_req(req,loop)  uv_loop_t* loop = async_loop(); \
                               with_req(uv_fs_t,req)



void async_await_file(uv_fs_t* req, uv_file owner) {
  async_await_owned((uv_req_t*)req, (void*)((intptr_t)owner));
}




/*-----------------------------------------------------------------
  Stat
-----------------------------------------------------------------*/

uv_errno_t asyncx_stat(const char* path, uv_stat_t* stat ) {
  nodec_zero(uv_stat_t,stat);
  uv_errno_t err = 0;
  {with_fs_req(req, loop){
    nodec_check(uv_fs_stat(loop, req, path, &async_fs_resume));
    err = asyncx_await_fs(req);
    if (err == 0) *stat = req->statbuf;
  }}
  return err; 
}

uv_stat_t async_stat(const char* path) {
  uv_stat_t stat;
  nodec_check_msg(asyncx_stat(path, &stat), path);
  return stat;
}

uv_stat_t async_fstat(uv_file file) {
  uv_stat_t stat; 
  nodec_zero(uv_stat_t, &stat);
  {with_fs_req(req, loop) {
    nodec_check(uv_fs_fstat(loop, req, file, &async_fs_resume));
    async_await_fs(req);
    stat = req->statbuf;
  }}  
  return stat;
}


/*-----------------------------------------------------------------
  Open, close, reading
-----------------------------------------------------------------*/


uv_errno_t asyncx_fopen(const char* path, int flags, int mode, uv_file* file) {
  *file = -1;
  uv_errno_t err = 0;
  {with_fs_req(req, loop) {
    nodec_check_msg(uv_fs_open(loop, req, path, flags, mode, &async_fs_resume), path);
    err = asyncx_await_fs(req);
    if (err == 0) *file = (uv_file)(req->result);
  }}
  return err;
}

uv_file async_fopen(const char* path, int flags, int mode) {
  uv_file file = -1;
  nodec_check_msg(asyncx_fopen(path, flags, mode, &file), path);
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
    {with_fs_req(req, loop) {
      nodec_check(uv_fs_close(loop, req, file, &async_fs_resume));
      async_await_file(req, file);
    }}
  }}
}

size_t async_fread(uv_file file, uv_buf_t* buf, int64_t file_offset) {
  size_t read = 0;
  {with_fs_req(req, loop) {
    nodec_check(uv_fs_read(loop, req, file, buf, 1, file_offset, &async_fs_resume));
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
  if (stat.st_size >= MAXSIZE_T) nodec_check(UV_E2BIG);
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

/*-----------------------------------------------------------------
  Scan dir
-----------------------------------------------------------------*/

void nodec_scandir_free(nodec_scandir_t* scanreq) {
  nodec_req_free((uv_req_t*)scanreq);
}
void nodec_scandir_freev(lh_value scanreqv) {
  nodec_scandir_free((nodec_scandir_t*)lh_ptr_value(scanreqv));
}

nodec_scandir_t* async_scandir(const char* path ) {
  uv_fs_t* fsreq = nodec_zero_alloc(uv_fs_t);
  {on_abort(nodec_freev, lh_value_ptr(fsreq)) {
    nodec_check(uv_fs_scandir(async_loop(), fsreq, path, 0, &async_fs_resume));
    async_await_fs(fsreq);
  }}
  return fsreq;
}

// Returns false if there are no more directories to scan;
bool async_scandir_next(nodec_scandir_t* scanreq, uv_dirent_t* dirent) {
  uverr_t err = uv_fs_scandir_next(scanreq, dirent);
  if (err == UV_EOF) return false;
  nodec_check(err);
  return true;
}

/*-----------------------------------------------------------------
  Efficient stack data structure that allocates in blocks.
  Can be pushed to, popped from, and iterated over the entire 
  contents.

  Declare "nodec_stack_dirent" type for `uv_diren_t` elements:
  ```
  NODEC_STACK_DECLARE(dirent,uv_dirent_t,64)
  ```
  and define its implemenation as:
  ```
  NODEC_STACK_DEFINE(dirent,uv_dirent_t,64)
  ```
  Use as:
  ```
  NODEC_STACK(dirent) stack;
  NODEC_STACK_MEMBER(dirent,init)(&stack,NULL);
  
  uv_dirent_t dirent;
  NODEC_STACK_MEMBER(dirent,push)(&stack, dirent);

  size_t index;
  NODEC_STACK_ITER(dirent) iter;
  NODEC_STACK_MEMBER(dirent,iterate)(&stack, &iter);
  while( NODEC_STACK_MEMBER(dirent,next)(&iter, &dirent, &index ) ) {
    ...
  }
  ```
-----------------------------------------------------------------*/

#define NODEC_STACK_BLOCK(T,sz)    nodec_stack_block_##T##_##sz
#define NODEC_STACK_ITER(T)        nodec_stack_iter_##T
#define NODEC_STACK(T)             nodec_stack_##T
#define NODEC_STACK_MEMBER(T,name) nodec_stack_##T##_##name

#define NODEC_STACK_DECLARE(T,TP,sz) \
  typedef struct _nodec_stack_block_##T { \
    struct _nodec_stack_block_##T* next; \
    struct _nodec_stack_block_##T* prev; \
    TP elems[sz]; \
  } NODEC_STACK_BLOCK(T,sz); \
  typedef struct _nodec_stack_##T { \
    NODEC_STACK_BLOCK(T,sz) first; \
    NODEC_STACK_BLOCK(T,sz)* last; \
    size_t count; \
    size_t last_count; \
    void   (*free_fun)(TP elem); \
  } NODEC_STACK(T); \
  typedef struct _nodec_stack_iter_##T { \
    NODEC_STACK_BLOCK(T,sz)* current; \
    size_t block_index; \
    size_t todo; \
    size_t index; \
  } NODEC_STACK_ITER(T); \
  size_t NODEC_STACK_MEMBER(T,count)(NODEC_STACK(T)* stack); \
  bool   NODEC_STACK_MEMBER(T,empty)(NODEC_STACK(T)* stack); \
  void   NODEC_STACK_MEMBER(T,push)(NODEC_STACK(T)* stack, TP elem); \
  bool   NODEC_STACK_MEMBER(T,pop)(NODEC_STACK(T)* stack, TP* elem); \
  bool   NODEC_STACK_MEMBER(T,at)(NODEC_STACK(T)* stack, size_t index, TP* elem); \
  void   NODEC_STACK_MEMBER(T,free)(NODEC_STACK(T)* stack); \
  void   NODEC_STACK_MEMBER(T,init)(NODEC_STACK(T)* stack, void (*free_fun)(TP elem) ); \
  void   NODEC_STACK_MEMBER(T,iterate)(NODEC_STACK(T)* stack, NODEC_STACK_ITER(T)* iter); \
  bool   NODEC_STACK_MEMBER(T,next)(NODEC_STACK_ITER(T)* iter, TP* elem, size_t* index ); \

#define NODEC_STACK_DEFINE(T,TP,sz) \
  size_t NODEC_STACK_MEMBER(T,count)(NODEC_STACK(T)* stack) { \
    return stack->count; \
  } \
  bool NODEC_STACK_MEMBER(T,empty)(NODEC_STACK(T)* stack) { \
    return (stack->count == 0); \
  } \
  void NODEC_STACK_MEMBER(T,push)(NODEC_STACK(T)* stack, TP elem) { \
    if (stack->last==NULL) stack->last = &stack->first; \
    if (stack->last_count >= sz) { \
      /* expand */ \
      NODEC_STACK_BLOCK(T,sz)* block = nodec_zero_alloc(NODEC_STACK_BLOCK(T,sz)); \
      stack->last->next = block; \
      block->prev = stack->last; \
      stack->last = block; \
      stack->last_count = 0; \
      \
    } \
    stack->last->elems[stack->last_count] = elem; \
    stack->count++; \
    stack->last_count++; \
  } \
  bool NODEC_STACK_MEMBER(T,pop)(NODEC_STACK(T)* stack, TP* elem) { \
    if (stack->last==NULL) stack->last = &stack->first; \
    if (stack->count == 0) return false; \
    assert(stack->last_count > 0 && stack->count >= stack->last_count); \
    if (stack->last_count == 0) return false; \
    stack->count--; \
    stack->last_count--; \
    if (elem != NULL) *elem = stack->last->elems[stack->last_count]; \
    if (stack->last_count == 0 && stack->last != &stack->first) { \
      /* contract */ \
      NODEC_STACK_BLOCK(T, sz)* block = stack->last; \
      stack->last = block->prev; \
      stack->last->next = NULL; \
      assert(stack->count >= sz); \
      stack->last_count = sz; \
      nodec_free(block); \
    } \
    return true; \
  } \
  bool NODEC_STACK_MEMBER(T,at)(NODEC_STACK(T)* stack, size_t index, TP* elem) { \
    if (stack->last==NULL) stack->last = &stack->first; \
    if (index >= stack->count) return false; \
    if (elem==NULL) return true; \
    NODEC_STACK_BLOCK(T,sz)* current = &stack->first; \
    while(index > sz && current != NULL) { \
      current = current->next; \
    } \
    assert(current != NULL); \
    if (current == NULL) return false; \
    assert((current != stack->last && index < sz) || (current==stack->last && index < stack->last_count)); \
    *elem = current->elems[index]; \
    return true; \
  } \
  void NODEC_STACK_MEMBER(T,free)(NODEC_STACK(T)* stack) { \
    for( NODEC_STACK_BLOCK(T,sz)* current = &stack->first; current != NULL;  ) {\
      if (stack->free_fun != NULL) { \
        size_t max = (current==stack->last ? stack->last_count : sz); \
        for(size_t i = 0; i < max; i++) { stack->free_fun(current->elems[i]); } \
      } \
      NODEC_STACK_BLOCK(T,sz)* next = current->next; \
      if (current != &stack->first) nodec_free(current); \
      current = next; \
    } \
    NODEC_STACK_MEMBER(T,init)(stack, stack->free_fun); \
  } \
  void NODEC_STACK_MEMBER(T,init)(NODEC_STACK(T)* stack, void (*free_fun)(TP elem)) { \
    stack->last = &stack->first; \
    stack->count = 0; \
    stack->last_count = 0; \
    stack->free_fun = free_fun; \
  } \
  void NODEC_STACK_MEMBER(T,iterate)(NODEC_STACK(T)* stack, NODEC_STACK_ITER(T)* iter) { \
    iter->current = &stack->first; \
    iter->todo = stack->count; \
    iter->index = 0; \
    iter->block_index = 0; \
  } \
  bool NODEC_STACK_MEMBER(T,next)(NODEC_STACK_ITER(T)* iter, TP* elem, size_t* index ) { \
    if (index != NULL) *index = iter->index; \
    if (iter->todo == 0) return false; \
    if (iter->block_index >= sz) { \
      iter->current = iter->current->next; \
      assert(iter->current != NULL); \
      if (iter->current == NULL) { iter->todo = 0; return false; }; \
    } \
    if (elem!=NULL) *elem = iter->current->elems[iter->block_index]; \
    iter->block_index++; \
    iter->index++; \
    iter->todo--; \
    return true; \
  } \

NODEC_STACK_DECLARE(dirent, uv_dirent_t, 64);
NODEC_STACK_DEFINE(dirent, uv_dirent_t, 64);


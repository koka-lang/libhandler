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

/*-----------------------------------------------------------------
Interleave
-----------------------------------------------------------------*/


// The channel async handler
// Resume by emmitting a local resume into a channel
void _channel_async_req_resume(lh_resume r, lh_value local, uv_req_t* req, int err) {
  assert(r != NULL);
  assert(local != lh_value_null);
  if (r != NULL) {
    channel_elem elem = { lh_value_ptr(r), local, lh_value_int(err) };
    channel_emit((channel_t*)lh_ptr_value(local), elem);
  }
}

static lh_value _channel_async_handler(channel_t* channel, lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&_channel_async_hdef, lh_value_ptr(channel), action, arg);
}

typedef struct _interleave_strand_args {
  lh_actionfun*  action;
  lh_value*      arg_res;
  lh_exception** exception;
  ssize_t*       todo;
} interleave_strand_args;

static lh_value _interleave_strand(lh_value vargs) {
  interleave_strand_args* args = (interleave_strand_args*)lh_ptr_value(vargs);
  lh_value arg = *args->arg_res;
  ssize_t* todo = args->todo;
  *args->arg_res = lh_value_null;
  *args->exception = NULL;
  *args->arg_res = lh_try_all(args->exception, args->action, arg);
  *todo = *todo - 1;
  return lh_value_null;
}

static void _handle_interleave_strand(channel_t* channel, interleave_strand_args* args) {
  _channel_async_handler(channel, &_interleave_strand, lh_value_any_ptr(args));
}

static void _interleave_n(size_t n, lh_actionfun** actions, lh_value* arg_results, lh_exception** exceptions) {
  {with_alloc(size_t, todo) {
    *todo = n;
    {with_channel(channel) {      
      for (size_t i = 0; i < n; i++) {
        interleave_strand_args args = {
          actions[i],
          &arg_results[i],
          &exceptions[i],
          todo
        };
        _handle_interleave_strand(channel, &args);
      }
      while (*todo > 0) {
        channel_elem res = channel_receive(channel);
        lh_release_resume((lh_resume)lh_ptr_value(res.data), res.arg, lh_value_int(res.err));
      }
    }}
  }}
}

static void nodec_free_if_notnull(lh_value pv) {
  if (pv!=lh_value_null) nodec_freev(pv);
}

void interleave(size_t n, lh_actionfun* actions[], lh_value arg_results[]) {
  if (n == 0 || actions == NULL) return;
  if (n == 1) {
    lh_value res = (actions[0])(arg_results==NULL ? lh_value_null : arg_results[0]);
    if (arg_results!=NULL) arg_results[0] = res;
  }
  else {
    lh_exception* exn = NULL;
    lh_value* local_args = NULL;
    lh_exception* local_exns = NULL;
    if (arg_results==NULL) {
      local_args = nodec_calloc(n, sizeof(lh_value));
      arg_results = local_args;
    }
    {defer(&nodec_free_if_notnull,lh_value_ptr(local_args)){
      {with_ncalloc(n, lh_exception*, exceptions) {
        _interleave_n(n, actions, arg_results, exceptions);
        // rethrow the first exception and release the others
        for (size_t i = 0; i < n; i++) {
          if (exceptions[i] != NULL) {
            if (exn == NULL) {
              exn = exceptions[i];
            }
            else {
              lh_exception_free(exceptions[i]);
            }
          }
        }
      }}      
    }}
    if (exn != NULL) lh_throw(exn);
  }
}

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


// The local async handler
// Local await an asynchronous request
void _local_async_resume_request(lh_resume r, lh_value local, uv_req_t* req, int err) {
  assert(r != NULL);
  assert(local != lh_value_null);
  if (r != NULL) {
    lh_channel_elem elem = { lh_value_ptr(r), local, lh_value_int(err) };
    lh_channel_emit((lh_channel*)lh_ptr_value(local), &elem);
  }
}

lh_value _local_async_handler(lh_channel* channel, lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&_local_async_hdef, lh_value_ptr(channel), action, arg);
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

static void _handle_interleave_strand(lh_channel* channel, interleave_strand_args* args) {
  _local_async_handler(channel, &_interleave_strand, lh_value_any_ptr(args));
}

static void _interleave_n(ssize_t n, lh_actionfun** actions, lh_value* arg_results, lh_exception** exceptions) {
  {with_alloc(ssize_t, todo) {
    *todo = n;
    {with_channel(channel) {      
      for (int i = 0; i < n; i++) {
        interleave_strand_args args = {
          actions[i],
          &arg_results[i],
          &exceptions[i],
          todo
        };
        _handle_interleave_strand(channel, &args);
      }
      while (*todo > 0) {
        lh_channel_elem res = lh_channel_receive(channel);
        lh_release_resume((lh_resume)lh_ptr_value(res.data), res.arg, lh_value_int(res.err));
      }
    }}
  }}
}

void interleave(ssize_t n, lh_actionfun** actions) {
  if (n <= 0 || actions == NULL) return;
  if (n == 1) {
    (actions[0])(lh_value_null);
  }
  else {
    lh_exception* exn = NULL;
    {with_ncalloc(n, lh_value, arg_results) {
      {with_ncalloc(n, lh_exception*, exceptions) {
        _interleave_n(n, actions, arg_results, exceptions);
        // rethrow the first exception and release the others
        for (ssize_t i = 0; i < n; i++) {
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

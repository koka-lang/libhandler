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
        // a receive should never be canceled since it should wait until
        // it children are canceled (and then continue). 
        channel_elem res = channel_receive_nocancel(channel);
        if (res.data != lh_value_null) { // can happen on cancel
          lh_release_resume((lh_resume)lh_ptr_value(res.data), res.arg, lh_value_int(res.err));
        }
      }
    }}
  }}
}

static void nodec_free_if_notnull(lh_value pv) {
  if (pv!=lh_value_null) nodec_freev(pv);
}

void interleave_n(size_t n, lh_actionfun* actions[], lh_value arg_results[], lh_exception* exceptions[]) {
  if (n == 0 || actions == NULL) return;

  lh_exception* exn = NULL;
  lh_value* local_args = NULL;
  lh_exception** local_exns = NULL;
  if (arg_results==NULL) {
    local_args = nodec_calloc(n, sizeof(lh_value));
    arg_results = local_args;
  }
  {defer(&nodec_free_if_notnull, lh_value_ptr(local_args)) {
    if (exceptions==NULL) {
      local_exns = nodec_calloc(n, sizeof(lh_exception*));
      exceptions = local_exns;
    }
    {defer(&nodec_free_if_notnull, lh_value_ptr(local_exns)) {
      _interleave_n(n, actions, arg_results, exceptions);
    }}
  }}
}

void interleave(size_t n, lh_actionfun* actions[], lh_value arg_results[]) {
  if (n == 0 || actions == NULL) return;
  if (n == 1) {
    lh_value res = (actions[0])(arg_results==NULL ? lh_value_null : arg_results[0]);
    if (arg_results!=NULL) arg_results[0] = res;
  }
  else {
    lh_exception* exn = NULL;
    {with_zero_alloc_n(n, lh_exception*, exceptions) {
      interleave_n(n, actions, arg_results, exceptions);
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
    if (exn != NULL) lh_throw(exn);
  }
}

lh_value async_firstof(lh_actionfun* action1, lh_value arg1, lh_actionfun* action2, lh_value arg2, bool* first ) {
  lh_actionfun* actions[2]     = { action1, action2 };
  lh_value      arg_results[2] = { arg1, arg2 };
  lh_exception* exceptions[2]  = { NULL, NULL };
  {with_cancel_scope() {
    interleave_n(2, actions, arg_results, exceptions);
  }}
  if (exceptions[0] != NULL) {
    if (first) *first = false;
    lh_exception_free(exceptions[0]);
    if (exceptions[1]!=NULL) lh_throw(exceptions[1]);
    return arg_results[1];
  }
  else {
    if (first) *first = true;
    if (exceptions[1]!=NULL) lh_exception_free(exceptions[1]);
    if (exceptions[0]!=NULL) lh_throw(exceptions[0]);
    return arg_results[0];
  }
}

typedef struct _timeout_action_args {
  lh_actionfun* action;
  lh_value      arg;
} timeout_action_args;

static lh_value _timeout_action(lh_value actionargsv) {
  timeout_action_args* args = (timeout_action_args*)lh_ptr_value(actionargsv);
  lh_value result = args->action(args->arg);
  async_scoped_cancel();
  return result;
}

static lh_value _timeout_wait(lh_value timeoutv) {
  uint64_t timeout = lh_longlong_value(timeoutv);
  async_delay(timeout);
  async_scoped_cancel();
  return lh_value_null;
}

lh_value async_timeout(lh_actionfun* action, lh_value arg, uint64_t timeout, bool* timedout) {
  timeout_action_args args = { action, arg };
  return async_firstof(_timeout_wait, lh_value_longlong(timeout), _timeout_action, lh_value_any_ptr(&args), timedout);
}
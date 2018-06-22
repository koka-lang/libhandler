/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

#pragma once
#ifndef __nodec_internal_h
#define __nodec_internal_h

#include "nodec.h"
// ---------------------------------------------------------------------------------
// Private: todo: is there a better way to define internally only visible routines?

lh_value   _channel_async_handler(channel_t* channel, lh_actionfun* action, lh_value arg);
void       _channel_async_req_resume(lh_resume r, lh_value local, uv_req_t* req, uverr err);

// These are callback functions to resume requests:
// Calling this will resume the `async_await` call on that request. 
// A call to these will resume at most once! (and be ignored after that)
void       async_req_resume(uv_req_t* uvreq, uverr err);
void       async_fs_resume(uv_fs_t* req);

void       nodec_req_force_free(uv_req_t* uvreq);
void       nodec_req_force_freev(lh_value uvreq);

void       nodec_req_free(uv_req_t* uvreq);
void       nodec_req_freev(lh_value uvreq);

#define with_req(req_tp,name) \
  req_tp* name = nodec_zero_alloc(req_tp); \
  defer(nodec_req_freev,lh_value_ptr(name))

#define with_free_req(req_tp,name) \
  req_tp* name = nodec_zero_alloc(req_tp); \
  defer(nodec_req_force_freev,lh_value_ptr(name))


// Await an asynchronous request but return an explicit error value instead of throwing.
// Use with care since these still throw on cancelation requests.
uverr   asyncx_nocancel_await(uv_req_t* uvreq);
uverr   asyncx_await(uv_req_t* req);
uverr   asyncx_await_fs(uv_fs_t* req);

// Set a timeout callback 
typedef void uv_timeoutfun(void* arg);
uverr   _uv_set_timeout(uv_loop_t* loop, uv_timeoutfun* cb, void* arg, uint64_t timeout);

int     channel_receive_nocancel(channel_t* channel, lh_value* data, lh_value* arg);

#define UV_ETHROWCANCEL  (-10000)
#define UV_EHTTP         (-20000)

#endif
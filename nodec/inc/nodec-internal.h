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

const lh_handlerdef _channel_async_hdef;
void                _channel_async_req_resume(lh_resume r, lh_value local, uv_req_t* req, uverr err);

// These are callback functions to resume requests:
// Calling this will resume the `async_await` call on that request. 
// A call to these will resume at most once! (and be ignored after that)
void       async_req_resume(uv_req_t* uvreq, uverr err);
void       async_fs_resume(uv_fs_t* req);

// Check the an error value and throw if it is not zero.
void       check_uv_err(uverr err);
void       check_uv_errmsg(uverr err, const char* msg);

// Await an asynchronous request but return an explicit error value instead of throwing.
// Use with care since these still throw on cancelation requests.
uverr   asyncx_await(uv_req_t* req);
uverr   asyncx_await_fs(uv_fs_t* req);

// Set a timeout callback
void    nodec_timer_free(uv_timer_t* timer);
uverr   _uv_set_timeout(uv_loop_t* loop, uv_timer_cb cb, void* arg, uint64_t timeout);

#define UV_ETHROWCANCEL  (-10000)

#endif
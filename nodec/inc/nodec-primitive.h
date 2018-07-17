/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

#pragma once
#ifndef __nodec_primitive_h
#define __nodec_primitive_h

#include "nodec.h"

// ---------------------------------------------------------------------------------
// NodeC primitive functions that might be used by certain clients
// but are not generally exposed.
// ---------------------------------------------------------------------------------

typedef int uverr_t;

// Check the an error value and throw if it is not zero.
void nodec_check(uverr_t err);
void nodec_check_msg(uverr_t err, const char* msg);

#if (defined(_WIN32) || defined(_WIN64))
typedef ULONG  uv_buf_len_t;
#else
typedef size_t uv_buf_len_t;
#endif


/* ----------------------------------------------------------------------------
Asynchronous primitives
-----------------------------------------------------------------------------*/

// Return the current event loop (ambiently bound by the async handler)
uv_loop_t* async_loop();

// Await an asynchronous request. Throws on error. 
// If canceled, the request is deallocated when the original callback is invoked.
// This is used for 'one of' callbacks, like `fs_stat`.
void       async_await_once(uv_req_t* req);

// Await an asynchronous request. 
// If canceled, the request is deallocated when the `owner` (usually a `uv_handle_t*`)
// is released. This is used for streams or timers.
void       async_await_owned(uv_req_t* req, void* owner);

/* ----------------------------------------------------------------------------
Channels
-----------------------------------------------------------------------------*/
typedef void (channel_release_elem_fun)(lh_value data, lh_value arg, int err);

channel_t*    channel_alloc(ssize_t queue_max);
channel_t*    channel_alloc_ex(ssize_t queue_max, lh_releasefun* release, lh_value release_arg, channel_release_elem_fun* release_elem);
void          channel_free(channel_t* channel);
void          channel_freev(lh_value vchannel);
#define with_channel(name) channel_t* name = channel_alloc(-1); defer(&channel_freev,lh_value_ptr(name))

uv_errno_t         channel_emit(channel_t* channel, lh_value data, lh_value arg, int err);
int           channel_receive(channel_t* channel, lh_value* data, lh_value* arg);
bool          channel_is_full(channel_t* channel);


// Used to implement keep-alive
uverr_t asyncx_stream_await_available(uv_stream_t* stream, uint64_t timeout);


#endif
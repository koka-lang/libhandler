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

const lh_handlerdef _local_async_hdef;
void                _local_async_resume_request(lh_resume r, lh_value local, uv_req_t* req, int err);

void       _async_fs_cb(uv_fs_t* req);
void       _async_plain_cb(uv_req_t* uvreq, int err);

void       check_uv_err(int err);
void       check_uv_errmsg(int err, const char* msg);

int        asyncx_await(uv_req_t* req);
int        asyncx_await_fs(uv_fs_t* req);
int        asyncx_await_connect(uv_connect_t* req);

#endif
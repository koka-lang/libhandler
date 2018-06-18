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

static void _timer_cb(uv_timer_t* timer) {
  uv_req_t* req = (uv_req_t*)timer->data;
  _async_plain_cb(req, 0);
}

static void _timer_close_cb(uv_handle_t* timer) {
  nodec_free(timer);
}

static void _timer_free(lh_value timerv) {
  uv_timer_t* timer = (uv_timer_t*)lh_ptr_value(timerv);
  uv_close(timer, &_timer_close_cb);
}

void async_delay(uint64_t timeout) {
  uv_timer_t* timer = nodec_zalloc(uv_timer_t);
  {defer(_timer_free,lh_value_ptr(timer)){
    {with_zalloc(uv_req_t, req) {
      check_uv_err(uv_timer_init(async_loop(), timer));
      timer->data = req;
      check_uv_err(uv_timer_start(timer, &_timer_cb, timeout, 0));
      async_await(req);
    }}
  }}
}

void async_yield() {
  async_delay(0);
}

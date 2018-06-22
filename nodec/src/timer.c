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


uverr _uv_set_timeout(uv_loop_t* loop, uv_timer_cb cb, void* arg, uint64_t timeout) {
  uv_timer_t* timer = (uv_timer_t*)calloc(1, sizeof(uv_timer_t));
  if (timer == NULL) return UV_ENOMEM;
  uv_timer_init(loop, timer);
  timer->data = arg;
  return uv_timer_start(timer, cb, timeout, 0);
}


uv_timer_t* nodec_timer_alloc() {
  uv_timer_t* timer = nodec_zero_alloc(uv_timer_t);
  check_uverr(uv_timer_init(async_loop(), timer));
  return timer;
}

static void _timer_close_cb(uv_handle_t* timer) {
  nodec_free(timer);
}

void nodec_timer_free(uv_timer_t* timer) {
  if (timer!=NULL) uv_close((uv_handle_t*)timer, &_timer_close_cb);
}

void nodec_timer_freev(lh_value timerv) {
  nodec_timer_free((uv_timer_t*)lh_ptr_value(timerv));
}

static void _async_timer_resume(uv_timer_t* timer) {
  uv_req_t* req = (uv_req_t*)timer->data;
  async_req_resume(req, 0);
}

void async_delay(uint64_t timeout) {
  uv_timer_t* timer = nodec_timer_alloc();
  {defer(nodec_timer_freev,lh_value_ptr(timer)){
    {with_free_req(uv_req_t, req) {  // use a dummy request so we can await the timer handle
                                     // and always free since we always close the timer too 
      timer->data = req;
      check_uverr(uv_timer_start(timer, &_async_timer_resume, timeout, 0));
      async_await(req);
    }}
  }}
}

void async_yield() {
  async_delay(0);
}

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
    Channels
-----------------------------------------------------------------*/

typedef void (lh_channel_listener)(lh_value arg, lh_channel_elem result);

typedef struct _lh_channel {
  lh_channel_listener* listener;
  lh_value             listener_arg;
  lh_channel_elem*     queue;
  ssize_t              queued;
  ssize_t              queue_size;
} lh_channel;

lh_channel* lh_channel_alloc() {
  lh_channel* channel = nodec_alloc(lh_channel);
  channel->listener = NULL;

  channel->queue = NULL;
  channel->queue_size = 0;
  channel->queued = 0;
  return channel;
}

void lh_channel_free(lh_channel* channel) {
  assert(channel->queued == 0);
  if (channel->queue != NULL) {
    free(channel->queue);
    channel->queue = NULL;
    channel->queue_size = 0;
  }
  free(channel);
}

void lh_channel_freev(lh_value vchannel) {
  lh_channel_free(lh_ptr_value(vchannel));
}

void lh_channel_emit(lh_channel* channel, lh_channel_elem* elem) {
  if (channel->listener != NULL) {
    // a listener, serve immediately
    lh_channel_listener* listener = channel->listener;
    channel->listener = NULL;
    listener(channel->listener_arg, *elem);
  }
  else {
    // otherwise queue it
    if (channel->queued >= channel->queue_size) {
      ssize_t newsize = (channel->queue_size > 0 ? 2 * channel->queue_size : 2);
      channel->queue = (channel->queue == NULL ? nodec_nalloc(newsize, lh_channel_elem)
        : nodec_realloc(channel->queue, newsize * sizeof(lh_channel_elem)));
      channel->queue_size = newsize;
    }
    channel->queue[channel->queued++] = *elem;
  }
}



typedef struct _uv_channel_req_t {
  uv_req_t req;  // must be the first element!
  lh_channel_elem elem;
} uv_channel_req_t;

static void _channel_req_listener(lh_value arg, lh_channel_elem elem) {
  uv_channel_req_t* req = (uv_channel_req_t*)lh_ptr_value(arg);
  req->elem = elem;
  _async_plain_cb(&req->req, 0 /* error for our channel */);
}

lh_channel_elem lh_channel_receive(lh_channel* channel) {
  lh_channel_elem result;
  nodec_zero(lh_channel_elem, &result);
  if (channel->listener != NULL) {
    assert(false);
    lh_throw_str(UV_ENOTSUP, "multiple listeners for a single channel");
  }
  else if (channel->queued>0) {
    // take top of the queue and continue
    result = channel->queue[--channel->queued];
  }
  else {
    // await the next emit
    uv_channel_req_t* req = nodec_ncalloc(1, uv_channel_req_t);
    {with_free(req) {
      channel->listener_arg = lh_value_ptr(req);
      channel->listener = &_channel_req_listener;
      async_await(&req->req);               // reqular await, triggered on channel_req_listener
      result = req->elem;
    }}
  }
  return result;
}
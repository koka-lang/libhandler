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

typedef void (channel_listener_fun)(lh_value arg, channel_elem result);
typedef struct _channel_listener {
  channel_listener_fun* fun;
  lh_value              arg;
} channel_listener;

typedef struct _channel_s {
  // listeners are a stack, so the last listeners
  // gets to handle first
  channel_listener* listeners;
  ssize_t           lcount;
  ssize_t           lsize;

  // the queue is a true queue
  channel_elem*     queue;
  ssize_t           qcount;
  ssize_t           qhead; // start index, wrap around
  ssize_t           qsize;
  ssize_t           qmax;

  // release
  lh_releasefun* release_fun;
  lh_value       release_arg;
  channel_release_elem_fun* release_elem;
} channel_t;

channel_t* channel_alloc(ssize_t queue_max) {
  return channel_alloc_ex(queue_max, NULL, lh_value_null, NULL);
}

channel_t* channel_alloc_ex( ssize_t queue_max, lh_releasefun* release_fun, lh_value release_arg, 
                          channel_release_elem_fun* release_elem ) 
{
  channel_t* channel = nodec_alloc(channel_t);
  channel->listeners = NULL;
  channel->lcount = 0;
  channel->lsize  = 0;
  channel->queue  = NULL;
  channel->qsize  = 0;
  channel->qcount = 0;
  channel->qhead  = 0;
  channel->qmax = (queue_max < 0 ? 1024*1024 : queue_max);
  channel->release_fun = release_fun;
  channel->release_arg = release_arg;
  channel->release_elem = release_elem;
  return channel;
}

void channel_free(channel_t* channel) {
  assert(channel->qcount == 0);
  assert(channel->lcount == 0);
  if (channel->queue != NULL) {
    if (channel->release_elem != NULL) {
      for (ssize_t i = 0; i < channel->qcount; i++) {
        ssize_t idx = i + channel->qhead;
        if (idx >= channel->qsize) idx = idx - channel->qsize;
        channel->release_elem(channel->queue[idx]);
      }
    }
    nodec_free(channel->queue);
    channel->queue = NULL;
    channel->qcount = channel->qhead= channel->qsize = 0;
  }
  if (channel->listeners != NULL) {
    channel_elem cancel = { lh_value_null,lh_value_null,UV_ECANCELED };
    for (ssize_t i = 0; i < channel->lcount; i++) {
      channel_listener* l = &channel->listeners[i];
      l->fun(l->arg, cancel);
    }
    nodec_free(channel->listeners);
    channel->listeners = NULL;
    channel->lcount = channel->lsize = 0;
  }
  if (channel->release_fun != NULL) {
    channel->release_fun(channel->release_arg);
  }
  nodec_free(channel);
}

void channel_freev(lh_value vchannel) {
  channel_free(lh_ptr_value(vchannel));
}

int channel_emit(channel_t* channel, channel_elem elem) {
  if (channel->lcount > 0) {
    // a listener, serve immediately
    // LIFO: good for web services but should perhaps be a parameter?
    channel->lcount--;
    const channel_listener* l = &channel->listeners[channel->lcount];
    l->fun(l->arg, elem);
    return 0;
  }
  else if (channel->qcount >= channel->qmax) {
    return UV_ENOSPC;
  }
  else {
    // otherwise queue it (FIFO buffer)
    if (channel->qcount >= channel->qsize) {
      ssize_t newsize = (channel->qsize > 0 ? 2 * channel->qsize : 2);
      channel_elem* newqueue = (channel_elem*)(channel->queue == NULL ? malloc(newsize * sizeof(channel_elem))
        : realloc(channel->queue, newsize * sizeof(channel_elem)));
      if (newqueue == NULL) return UV_ENOMEM;
      channel->queue = newqueue;
      channel->qsize = newsize;
    }
    ssize_t idx = (channel->qhead + channel->qcount);
    if (idx>=channel->qsize) idx = idx - channel->qsize;
    channel->queue[idx] = elem;
    channel->qcount++;
    return 0;
  }
}



typedef struct _uv_channel_req_t {
  uv_req_t req;  // must be the first element!
  channel_elem elem;
} uv_channel_req_t;

static void _channel_req_listener_fun(lh_value arg, channel_elem elem) {
  uv_channel_req_t* req = (uv_channel_req_t*)lh_ptr_value(arg);
  req->elem = elem;
  _async_plain_cb(&req->req, 0 /* error for our channel */);
}

channel_elem channel_receive(channel_t* channel) {
  channel_elem result;
  nodec_zero(channel_elem, &result);
  if (channel->qcount>0) {
    // take top of the queue and continue
    result = channel->queue[channel->qhead];
    channel->qcount--;
    channel->qhead++;
    if (channel->qhead >= channel->qsize) channel->qhead = 0;
  }
  else {
    // await the next emit
    uv_channel_req_t* req = nodec_ncalloc(1, uv_channel_req_t);
    {with_free(req){
      if (channel->lcount >= channel->lsize) {
        ssize_t newsize = (channel->lsize > 0 ? 2 * channel->lsize : 2);
        channel->listeners = (channel->listeners == NULL ? nodec_nalloc(newsize, channel_listener)
          : (channel_listener*)nodec_realloc(channel->listeners, newsize*sizeof(channel_listener)));
        channel->lsize = newsize;
      }
      channel->listeners[channel->lcount].arg = lh_value_ptr(req);
      channel->listeners[channel->lcount].fun = &_channel_req_listener_fun;
      channel->lcount++;
      // and await our request 
      async_await(&req->req);               // reqular await, triggered on channel_req_listener
      result = req->elem;
    }}
  }
  return result;
}
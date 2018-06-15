#include "nodec.h"
#include "nodec-internal.h"
#include <uv.h>
#include <assert.h> 

/*-----------------------------------------------------------------
Interleave
-----------------------------------------------------------------*/

typedef struct _lh_channel_elem {
  lh_value data;
  lh_value local;
  int      err;
} lh_channel_elem;

typedef void (lh_channel_listener)(lh_value arg, lh_channel_elem result);

typedef struct _lh_channel {
  lh_channel_listener* listener;
  lh_value             listener_arg;
  lh_channel_elem*     queue;
  ssize_t              queued;
  ssize_t              queue_size;
} lh_channel;

static lh_channel* lh_channel_alloc() {
  lh_channel* channel = (lh_channel*)malloc(sizeof(lh_channel));
  channel->listener = NULL;

  channel->queue = NULL;
  channel->queue_size = 0;
  channel->queued = 0;
  return channel;
}

static void lh_channel_free(lh_channel* channel) {
  assert(channel->queued == 0);
  if (channel->queue != NULL) {
    free(channel->queue);
    channel->queue = NULL;
    channel->queue_size = 0;
  }
  free(channel);
}

static void lh_channel_emit(lh_channel* channel, lh_channel_elem* elem) {
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
      channel->queue = (channel->queue == NULL ? malloc(newsize) : realloc(channel->queue, newsize));
      channel->queue_size = newsize;
    }
    channel->queue[channel->queued++] = *elem;
    // todo: we lose the error value!
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

static void lh_channel_receive(lh_channel* channel, lh_channel_elem* result) {
  if (channel->listener != NULL) {
    assert(false);
    lh_throw(lh_exception_alloc(UV_ENOTSUP, "multiple listeners for a single channel"));
  }
  else if (channel->queued>0) {
    // take top of the queue and continue
    *result = channel->queue[--channel->queued];
  }
  else {
    // await the next emit
    uv_channel_req_t* req = (uv_channel_req_t*)calloc(1, sizeof(uv_channel_req_t));
    channel->listener_arg = lh_value_ptr(req);
    channel->listener = &_channel_req_listener;
    int err = asyncx_await(&req->req);               // reqular await, triggered on channel_req_listener
    *result = req->elem;
    free(req);
    check_uv_err(err);
  }
}


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

typedef struct _interleave1_args {
  lh_actionfun*  action;
  lh_value*      arg_res;
  lh_exception** exception;
  ssize_t*       todo;
} interleave1_args;

static lh_value _interleave1(lh_value vargs) {
  interleave1_args* args = (interleave1_args*)lh_ptr_value(vargs);
  lh_value arg = *args->arg_res;
  ssize_t* todo = args->todo;
  *args->arg_res = lh_value_null;
  *args->exception = NULL;
  *args->arg_res = lh_try(args->exception, args->action, arg);
  *todo = *todo - 1;
  return lh_value_null;
}

static void _handle_interleave1(lh_channel* channel, interleave1_args* args) {
  _local_async_handler(channel, &_interleave1, lh_value_any_ptr(args));
}

static void _interleave_n(ssize_t n, lh_actionfun** actions, lh_value* arg_results, lh_exception** exceptions) {
  ssize_t* todo = (size_t*)malloc(sizeof(ssize_t));
  *todo = n;
  lh_channel* channel = lh_channel_alloc();
  for (int i = 0; i<n; i++) {
    interleave1_args args = {
      actions[i],
      &arg_results[i],
      &exceptions[i],
      todo
    };
    _handle_interleave1(channel, &args);
  }
  while (*todo>0) {
    lh_channel_elem res;
    lh_channel_receive(channel, &res);
    lh_release_resume((lh_resume)lh_ptr_value(res.data), res.local, lh_value_int(res.err));
  }
  lh_channel_free(channel);
  free(todo);
}

void interleave(ssize_t n, lh_actionfun** actions) {
  if (n <= 0 || actions == NULL) return;
  if (n == 1) {
    (actions[0])(lh_value_null);
  }
  else {
    lh_value*      arg_results = (lh_value*)calloc(n, sizeof(lh_value));
    lh_exception** exceptions = (lh_exception**)calloc(n, sizeof(lh_exception*));
    _interleave_n(n, actions, arg_results, exceptions);
    lh_exception* exn = NULL;
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
    free(exceptions);
    free(arg_results);
    if (exn != NULL) lh_throw(exn);
  }
}

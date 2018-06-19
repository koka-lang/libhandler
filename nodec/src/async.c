#include "nodec.h"
#include "nodec-internal.h"
#include <uv.h>
#include <assert.h> 

/*-----------------------------------------------------------------
  Asynchronous requests
-----------------------------------------------------------------*/

typedef void(async_resume_fun)(lh_resume r, lh_value local, uv_req_t* req, int err);

typedef struct _async_scope_t {
  struct _async_scope_t* parent;
} async_scope_t;


// Every libuv request will have a pointer to our request structure in its
// `data` field. This allows us to resume to the `async_await` point.
// The actual resuming is done in the `reqfun` such that we can sometimes change
// the resume behaviour: normally it resumes but we can also emit results into 
// a channel for example.
typedef struct _async_request_t {
  struct async_request_t* next;
  struct async_request_t* prev;
  lh_resume           resume;
  lh_value            local;
  async_scope_t*      scope;
  uv_req_t*           uvreq;
  async_resume_fun*   resumefun;
} async_request_t;

static async_request_t* async_request_alloc(uv_req_t* uvreq) {
  async_request_t* req = nodec_zalloc(async_request_t);
  uvreq->data = req;
  req->uvreq = uvreq;
  // todo: init scope
  return req;
}

static void async_resume_default(lh_resume resume, lh_value local, uv_req_t* req, int err) {
  //lh_assert(r != NULL);
  if (resume != NULL) {
    lh_release_resume(resume, local, lh_value_int(err));
  }
}


static void async_request_resume(async_request_t* req, uv_req_t* uvreq, int err) {
  assert(req->uvreq == NULL || req->uvreq == uvreq);
  if (req->uvreq != NULL) {
    uvreq->data = NULL; // resume at most once
    req->uvreq = NULL;
    // todo: unregister request?
    async_resume_fun* resumefun = req->resumefun;
    lh_resume resume = req->resume;
    lh_value local = req->local;
    free(req);
    (*resumefun)(resume, local, uvreq, err);
  }
}



// The entry point for regular request callbacks which will resume
// to the point where `async_await` was called on that request.
void async_req_resume(uv_req_t* uvreq, int err) {
  async_request_t* req = (async_request_t*)uvreq->data;
  if (req != NULL) async_request_resume(req, uvreq, err);
}



/*-----------------------------------------------------------------
  Async effect operations
-----------------------------------------------------------------*/
typedef uv_loop_t*   uv_loop_ptr;
typedef uv_req_t*    uv_req_ptr;
typedef uv_handle_t* uv_handle_ptr;
typedef async_request_t* async_request_ptr;

#define lh_uv_loop_ptr_value(v)     ((uv_loop_t*)lh_ptr_value(v))
#define lh_value_uv_loop_ptr(h)     lh_value_ptr(h)
#define lh_async_request_ptr_value(v)      ((async_request_t*)lh_ptr_value(v))
#define lh_value_async_request_ptr(r)      lh_value_ptr(r)

LH_DEFINE_EFFECT2(async, uv_await, uv_loop);
LH_DEFINE_OP0(async, uv_loop, uv_loop_ptr);
LH_DEFINE_OP1(async, uv_await, int, async_request_ptr);




// Wrappers around the primitive operations for async.
uv_loop_t* async_loop() {
  return async_uv_loop();
}

uverr asyncx_await(uv_req_t* uvreq) {
  async_request_t* req = async_request_alloc(uvreq);
  return async_uv_await(req);
}

void async_await(uv_req_t* req) {
  check_uv_err(asyncx_await(req));
}


/*-----------------------------------------------------------------
  Throw on errors
-----------------------------------------------------------------*/


// Check an error result, throwing on error
void check_uv_err(uverr uverr) {
  if (uverr < 0) {
    lh_throw(lh_exception_alloc_strdup(uverr, uv_strerror(uverr)));
  }
}

// Check an error result, throwing on error
void check_uv_errmsg(uverr uverr, const char* msg) {
  if (uverr < 0) {
    char buf[256];
    snprintf(buf, 255, "%s: %s", uv_strerror(uverr), msg);
    buf[255] = 0;
    lh_throw(lh_exception_alloc_strdup(uverr, buf));
  }
}



// Await an asynchronous request
static lh_value _async_uv_await(lh_resume resume, lh_value local, lh_value arg) {
  async_request_t* req = lh_async_request_ptr_value(arg);
  assert(req != NULL);
  assert(req->uvreq != NULL);
  assert(req->uvreq->data == req);
  req->local = local;
  req->resume = resume;
  // todo: register request
  if (req->resumefun==NULL) req->resumefun = &async_resume_default;
  return lh_value_null;  // this exits our async handler back to the main event loop
}

// Return the current libUV event loop
static lh_value _async_uv_loop(lh_resume r, lh_value local, lh_value arg) {
  return lh_tail_resume(r, local, local);
}

// The main async handler
static const lh_operation _async_ops[] = {
  { LH_OP_GENERAL, LH_OPTAG(async,uv_await), &_async_uv_await },
  { LH_OP_TAIL_NOOP, LH_OPTAG(async,uv_loop), &_async_uv_loop },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef _async_def = { LH_EFFECT(async), NULL, NULL, NULL, _async_ops };

lh_value async_handler(uv_loop_t* loop, lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&_async_def, lh_value_uv_loop_ptr(loop), action, arg);
}


/*-----------------------------------------------------------------
  Channel async handler for interleave
-----------------------------------------------------------------*/

static lh_value _channel_async_uv_await(lh_resume r, lh_value local, lh_value arg) {
  async_request_t* req = lh_async_request_ptr_value(arg);
  assert(req != NULL);
  assert(req->uvreq != NULL);
  assert(req->uvreq->data == req);
  req->resume = r;
  req->local = local;
  if (req->resumefun==NULL) req->resumefun = &_channel_async_req_resume;
  // todo: register request
  return lh_value_null;  // exit to our local async handler back to interleaved
}

// Return the current libUV event loop
static lh_value _channel_async_uv_loop(lh_resume r, lh_value local, lh_value arg) {
  return lh_tail_resume(r, local, lh_value_ptr(async_loop()));
}

static const lh_operation _channel_async_ops[] = {
  { LH_OP_GENERAL, LH_OPTAG(async,uv_await), &_channel_async_uv_await },
  { LH_OP_TAIL, LH_OPTAG(async,uv_loop), &_channel_async_uv_loop },
  { LH_OP_NULL, lh_op_null, NULL }
};
const lh_handlerdef _channel_async_hdef = { LH_EFFECT(async), NULL, NULL, NULL, _channel_async_ops };



/*-----------------------------------------------------------------
Main wrapper
-----------------------------------------------------------------*/
static lh_value uv_main_action(lh_value ventry) {
  nc_entryfun_t* entry = (nc_entryfun_t*)lh_ptr_value(ventry);
  entry();
  return lh_value_null;
}

static lh_value uv_main_try_action(lh_value entry) {
  lh_exception* exn;
  lh_try(&exn, uv_main_action, entry);
  if (exn != NULL) {
    printf("unhandled exception: %s\n", exn->msg);
    lh_exception_free(exn);     
  }
  return lh_value_null;
}

static void uv_main_cb(uv_timer_t* t_start) {
  // uv_mainx(t_start->loop);
  async_handler(t_start->loop, &uv_main_try_action, lh_value_ptr(t_start->data));
  uv_timer_stop(t_start);
}

void async_main( nc_entryfun_t* entry  ) {
  uv_loop_t* loop = uv_default_loop();
  uv_timer_t t_start;
  t_start.data = entry;
  uv_timer_init(loop, &t_start);
  uv_timer_start(&t_start, &uv_main_cb, 0, 0);
  printf("starting\n");
  int result = uv_run(loop, UV_RUN_DEFAULT);
  uv_loop_close(loop);

  nodec_check_memory();
  char buf[128];
  printf("done! (press enter to quit)\n"); gets(buf);
  return;
}
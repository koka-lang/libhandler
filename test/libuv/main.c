#include <stdio.h>
#include <fcntl.h>
#include <libhandler.h>
#include <uv.h>
#include <assert.h>

/*-----------------------------------------------------------------
Async effect
-----------------------------------------------------------------*/

typedef uv_handle_t* uv_handle_ptr;
typedef uv_loop_t*   uv_loop_ptr;
typedef uv_req_t*    uv_req_ptr;

#define lh_uv_handle_ptr_value(v)   ((uv_handle_t*)lh_ptr_value(v))
#define lh_value_uv_handle_ptr(h)   lh_value_ptr(h)
#define lh_uv_loop_ptr_value(v)     ((uv_loop_t*)lh_ptr_value(v))
#define lh_value_uv_loop_ptr(h)     lh_value_ptr(h)
#define lh_uv_req_ptr_value(v)      ((uv_req_t*)lh_ptr_value(v))
#define lh_value_uv_req_ptr(r)      lh_value_ptr(r)


LH_DEFINE_EFFECT2(async, uv_await, uv_loop);
LH_DEFINE_OP0(async, uv_loop, uv_loop_ptr);
LH_DEFINE_OP1(async, uv_await, int, uv_req_ptr);

// Await a file system request
int async_uv_await_fs(uv_fs_t* req) {
  return async_uv_await((uv_req_t*)req);
}

// Check an error result, throwing on error
void check_uv_err(int uverr) {
  if (uverr < 0) {
    lh_throw(lh_exception_alloc_strdup(uverr, uv_strerror(uverr)));
  }
}

// Check an error result, throwing on error
void check_uv_errmsg(int uverr, const char* msg) {
  if (uverr < 0) {
    char buf[256];
    snprintf(buf, 255, "%s: %s", uv_strerror(uverr), msg);
    buf[255] = 0;
    lh_throw(lh_exception_alloc_strdup(uverr, buf));
  }
}

/*-----------------------------------------------------------------
Async handler
-----------------------------------------------------------------*/
typedef void(lh_request_fun)(lh_resume r, lh_value local, uv_req_t* req, int err);

typedef struct _lh_request {
  lh_resume       resume;
  lh_value        local;
  lh_request_fun* reqfun;
} lh_request;

static void async_resume_request(lh_resume r, lh_value local, uv_req_t* req, int err) {
  //lh_assert(r != NULL);
  if (r != NULL) {
    lh_release_resume(r, local, lh_value_int(err));
  }
}

// The entry point for filesystem callbacks
static void async_fs_cb(uv_fs_t* uvreq) {
  lh_request* req = (lh_request*)uvreq->data;
  if (req != NULL) {
    uvreq->data = NULL; // resume at most once
    int err = (uvreq->result >= 0 ? 0 : (int)uvreq->result);
    lh_resume resume = req->resume;
    lh_value local = req->local;
    lh_request_fun* reqfun = req->reqfun;
    free(req);
    (*reqfun)(resume, local, (uv_req_t*)uvreq, err);    
  }
}

// The entry point for plain callbacks
static void async_plain_cb(uv_req_t* uvreq, int err) {
  lh_request* req = (lh_request*)uvreq->data;
  if (req != NULL) {
    uvreq->data = NULL; // resume at most once
    lh_resume resume = req->resume;
    lh_value local = req->local;
    lh_request_fun* reqfun = req->reqfun;
    free(req);
    (*reqfun)(resume, local, (uv_req_t*)uvreq, err);
  }
}

// Await an asynchronous request
static lh_value _async_uv_await(lh_resume r, lh_value local, lh_value arg) {
  uv_req_t* uvreq = lh_uv_req_ptr_value(arg);
  lh_request* req = (lh_request*)malloc(sizeof(lh_request));
  uvreq->data = req;
  req->resume = r;
  req->local = local;
  req->reqfun = &async_resume_request;
  return lh_value_null;  // this exits our async handler to the main event loop
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
    listener(channel->listener_arg,*elem);
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
  async_plain_cb(&req->req, 0 /* error for our channel */);
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
    uv_channel_req_t* req = (uv_channel_req_t*)calloc(1,sizeof(uv_channel_req_t)); 
    channel->listener_arg = lh_value_ptr(req);
    channel->listener = &_channel_req_listener;
    int err = async_uv_await(&req->req);               // reqular await, triggered on channel_req_listener
    *result = req->elem;
    free(req);
    check_uv_err(err);
  }
}


// The local async handler
// Local await an asynchronous request

static void _local_async_resume_request(lh_resume r, lh_value local, uv_req_t* req, int err) {
  assert(r != NULL);
  assert(local != lh_value_null);
  if (r != NULL) {
    lh_channel_elem elem = { lh_value_ptr(r), local, lh_value_int(err) };
    lh_channel_emit((lh_channel*)lh_ptr_value(local), &elem);
  }
}

static lh_value _local_async_uv_await(lh_resume r, lh_value local, lh_value arg) {
  uv_req_t* uvreq = lh_uv_req_ptr_value(arg);
  lh_request* req = (lh_request*)malloc(sizeof(lh_request));
  uvreq->data = req;
  req->resume = r;
  req->local = local;
  req->reqfun = *_local_async_resume_request;
  return lh_value_null;  // exit to our local async handler in interleaved
}

// Return the current libUV event loop
static lh_value _local_async_uv_loop(lh_resume r, lh_value local, lh_value arg) {
  return lh_tail_resume(r, local, lh_value_ptr(async_uv_loop()) );
}

static const lh_operation _local_async_ops[] = {
  { LH_OP_GENERAL, LH_OPTAG(async,uv_await), &_local_async_uv_await },
  { LH_OP_TAIL, LH_OPTAG(async,uv_loop), &_local_async_uv_loop },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef _local_async_def = { LH_EFFECT(async), NULL, NULL, NULL, _local_async_ops };

lh_value _local_async_handler(lh_channel* channel, lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&_local_async_def, lh_value_ptr(channel), action, arg);
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
  lh_handle(&_local_async_def, lh_value_ptr(channel), &_interleave1, lh_value_any_ptr(args) );
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
    lh_exception** exceptions  = (lh_exception**)calloc(n, sizeof(lh_exception*));    
    _interleave_n(n, actions, arg_results, exceptions );
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



/*-----------------------------------------------------------------
Async wrappers
-----------------------------------------------------------------*/

static uv_fs_t* uv_fs_alloc() {
  return (uv_fs_t*)malloc(sizeof(uv_fs_t));
}

static void uv_fs_free(uv_fs_t* req) {
  if (req != NULL) {
    uv_fs_req_cleanup(req);
    free(req);
  }
}

static int async_statx(const char* path, uv_stat_t* stat) {
  memset(stat, 0, sizeof(uv_stat_t));
  uv_fs_t*   req = uv_fs_alloc();
  uv_loop_t* loop = async_uv_loop();
  int err = uv_fs_stat(loop, req, path, &async_fs_cb);
  if (err == 0) {
    err = async_uv_await_fs(req);
    if (err == 0) *stat = req->statbuf;
  }
  uv_fs_free(req);
  return err; // UV_UNKNOWN
}

uv_stat_t async_stat(const char* path) {
  uv_stat_t stat;
  check_uv_errmsg(async_statx(path, &stat), path);
  return stat;
}

static int async_fstatx(uv_file file, uv_stat_t* stat) {
  memset(stat, 0, sizeof(uv_stat_t));
  uv_fs_t*   req = uv_fs_alloc();
  uv_loop_t* loop = async_uv_loop();
  int err = uv_fs_fstat(loop, req, file, &async_fs_cb);
  if (err == 0) {
    err = async_uv_await_fs(req);
    if (err == 0) *stat = req->statbuf;
  }
  uv_fs_free(req);
  return err;
}

uv_stat_t async_fstat(uv_file file) {
  uv_stat_t stat;
  check_uv_err(async_fstatx(file, &stat));
  return stat;
}

static int async_fopenx(const char* path, int flags, int mode, uv_file* file) {
  *file = -1;
  uv_fs_t* req = uv_fs_alloc();
  uv_loop_t* loop = async_uv_loop();
  int err = uv_fs_open(loop, req, path, flags, mode, &async_fs_cb);
  if (err == 0) {
    err = async_uv_await_fs(req);
    if (err == 0) *file = (uv_file)(req->result);
  }
  uv_fs_free(req);
  return err;
}

uv_file async_fopen(const char* path, int flags, int mode) {
  uv_file file = -1;
  check_uv_errmsg(async_fopenx(path, flags, mode, &file), path);
  return file;
}


static int async_fclosex(uv_file file) {
  if (file < 0) return 0;
  uv_fs_t*   req = uv_fs_alloc();
  uv_loop_t* loop = async_uv_loop();
  int err = uv_fs_close(loop, req, file, &async_fs_cb);
  if (err == 0) err = async_uv_await_fs(req);
  uv_fs_free(req);
  return err;
}

void async_fclose(uv_file file) {
  check_uv_err(async_fclosex(file));
}

static int async_freadx(uv_file file, uv_buf_t* buf, int64_t offset, ssize_t* read) {
  *read = 0;
  uv_fs_t* req = uv_fs_alloc();
  uv_loop_t* loop = async_uv_loop();
  int err = uv_fs_read(loop, req, file, buf, 1, offset, &async_fs_cb);
  if (err == 0) {
    err = async_uv_await_fs(req);
    if (err == 0) *read = req->result;
  }
  uv_fs_free(req);
  return err;
}

ssize_t async_fread(uv_file file, uv_buf_t* buf, int64_t offset) {
  ssize_t read = 0;
  check_uv_err(async_freadx(file, buf, offset, &read));
  return read;
}

static int async_fread_fullx(const char* path, ssize_t* len, char** contents) {
  *contents = NULL;
  *len = 0;
  uv_file file;
  int err = async_fopenx(path, O_RDONLY, 0, &file);
  if (err != 0) return err;
  uv_stat_t stat;
  err = async_fstatx(file, &stat);
  if (err == 0) {
    ssize_t size = stat.st_size;
    char*   buffer = (char*)malloc(size + 1);
    uv_buf_t buf = uv_buf_init(buffer, (unsigned)size);
    ssize_t read = 0;
    ssize_t total = 0;
    while (total < size && (err = async_freadx(file, &buf, -1, &read)) == 0 && read > 0) {
      total += read;
      if (total > size) {
        total = size;
      }
      else {
        buf = uv_buf_init(buffer + total, (unsigned)(size - total));
      }
    }
    buffer[total] = 0;
    if (err == 0) {
      *contents = buffer;
      *len = total;
    }
    else {
      free(buffer);
    }
  }
  async_fclosex(file);
  return err;
}

char* async_fread_full(const char* path) {
  char*   contents = NULL;
  ssize_t len = 0;
  check_uv_errmsg(async_fread_fullx(path, &len, &contents), path);
  return contents;
}

/*-----------------------------------------------------------------
Main
-----------------------------------------------------------------*/
static void test_stat() {
  const char* path = "cenv.hx";
  uv_stat_t stat = async_stat(path);
  printf("file %s last access time: %li\n", path, stat.st_atim.tv_sec);
}

static void test_fileread() {
  printf("opening file\n");
  char* contents = async_fread_full("cenv.h");
  printf("read %Ii bytes:\n%s\n", strlen(contents), contents);
  free(contents);
}

lh_value test_statx(lh_value arg) { 
  test_stat();
  return lh_value_null;
}
lh_value test_filereadx(lh_value arg) {
  test_fileread();
  return lh_value_null;
}

static void test_interleave() {
  lh_actionfun* actions[3] = { &test_statx, &test_filereadx, &test_filereadx };
  interleave(3, actions);
}

static void uv_main() {
  printf("in the main loop\n");
  //test_stat();
  //test_fileread();
  test_interleave();
}




/*-----------------------------------------------------------------
Main wrapper
-----------------------------------------------------------------*/
static lh_value uv_main_action(lh_value arg) {
  uv_main();
  return lh_value_null;
}

static lh_value uv_main_try_action(lh_value arg) {
  lh_exception* exn;
  lh_try(&exn, uv_main_action, arg);
  if (exn != NULL) {
    printf("unhandled exception: %s\n", exn->msg);
    lh_exception_free(exn);     
  }
  return lh_value_null;
}

static void uv_main_cb(uv_timer_t* t_start) {
  // uv_mainx(t_start->loop);
  async_handler(t_start->loop, &uv_main_try_action, lh_value_null);
  uv_timer_stop(t_start);
}

int main() {
  uv_loop_t* loop = uv_default_loop();
  uv_timer_t t_start;
  uv_timer_init(loop, &t_start);
  uv_timer_start(&t_start, &uv_main_cb, 0, 0);
  printf("starting\n");
  int result = uv_run(loop, UV_RUN_DEFAULT);
  uv_loop_close(loop);

  char buf[128];
  printf("done! (press enter to quit)\n"); gets(buf);
  return;
}
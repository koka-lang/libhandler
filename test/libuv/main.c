#include <stdio.h>
#include <fcntl.h>
#include <libhandler.h>
#include <uv.h>
#include <assert.h>

/*-----------------------------------------------------------------
Async effect
-----------------------------------------------------------------*/

typedef uv_loop_t*   uv_loop_ptr;
typedef uv_req_t*    uv_req_ptr;

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
void check_uverr(int uverr) {
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
  check_uverr(async_fstatx(file, &stat));
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
  check_uverr(async_fclosex(file));
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
  check_uverr(async_freadx(file, buf, offset, &read));
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
    size_t size = (size_t)stat.st_size;
    char*   buffer = (char*)malloc(size + 1);
    uv_buf_t buf = uv_buf_init(buffer, (unsigned)size);
    size_t read = 0;
    size_t total = 0;
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
  const char* path = "cenv.h";
  uv_stat_t stat = async_stat(path);
  printf("file %s last access time: %li\n", path, stat.st_atim.tv_sec);
}

static void test_fileread() {
  printf("opening file\n");
  char* contents = async_fread_full("cenv.h");
  printf("read %Ii bytes:\n%s\n", strlen(contents), contents);
  free(contents);
}

static void uv_main() {
  printf("in the main loop\n");
  test_stat();
  test_fileread();
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

  
  lh_debug_wait_for_enter();
  return;
}
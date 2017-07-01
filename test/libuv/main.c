#include <stdio.h>
#include <fcntl.h>
#include <libhandler.h>
#include <uv.h>

/*-----------------------------------------------------------------
  Async effect
-----------------------------------------------------------------*/

typedef uv_handle_t* uv_handle_ptr;
typedef uv_loop_t*   uv_loop_ptr;

#define lh_uv_handle_ptr_value(v)   ((uv_handle_t*)lh_ptr_value(v))
#define lh_value_uv_handle_ptr(h)   lh_value_ptr(h)
#define lh_uv_loop_ptr_value(v)   ((uv_loop_t*)lh_ptr_value(v))
#define lh_value_uv_loop_ptr(h)   lh_value_ptr(h)

LH_DEFINE_EFFECT2(async, uv_await, uv_loop);
LH_DEFINE_OP1(async, uv_await, int, uv_handle_ptr);
LH_DEFINE_OP0(async, uv_loop, uv_loop_ptr);


/*-----------------------------------------------------------------
  Async handler
-----------------------------------------------------------------*/

static void async_fs_cb(uv_fs_t* req) {
  lh_resume r = (lh_resume)(req->data);
  if (r != NULL) {
    int err = req->result >= 0 ? 0 : req->result;
    lh_release_resume(r, lh_value_uv_loop_ptr(req->loop), lh_value_int(err));
  }
}

static lh_value _async_uv_await(lh_resume r, lh_value local, lh_value arg) {
  uv_handle_t* h = lh_uv_handle_ptr_value(arg);
  h->data = r;
  return lh_value_null;
}

static lh_value _async_uv_loop(lh_resume r, lh_value local, lh_value arg) {
  return lh_tail_resume(r, local,local);
}

static const lh_operation _async_ops[] = {
  { LH_OP_GENERAL, LH_OPTAG(async,uv_await), &_async_uv_await},
  { LH_OP_TAIL_NOOP, LH_OPTAG(async,uv_loop), &_async_uv_loop},
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
  
static int async_fstat(const char* path, uv_stat_t* stat ) {
  memset(stat, 0, sizeof(uv_stat_t));
  uv_fs_t*   req = uv_fs_alloc();
  uv_loop_t* loop = async_uv_loop();
  int err = uv_fs_stat(loop, req, path, &async_fs_cb);
  if (err == 0) {
    err = async_uv_await(req);
    if (err==0) *stat = req->statbuf;
  }
  uv_fs_free(req);
  return err;
}

static int async_fopen(const char* path, int flags, int mode, uv_file* file) {
  *file = -1;
  uv_fs_t* req = uv_fs_alloc();
  uv_loop_t* loop = async_uv_loop();
  int err = uv_fs_open(loop, req, path, flags, mode, &async_fs_cb);
  if (err == 0) {
    err = async_uv_await(req);
    if (err==0) *file = req->result; 
  }
  uv_fs_free(req);
  return err;
}

static int async_fclose(uv_file file) {
  if (file < 0) return 0;
  uv_fs_t*   req  = uv_fs_alloc();
  uv_loop_t* loop = async_uv_loop();
  int err = uv_fs_close(loop, req, file, &async_fs_cb);
  if (err == 0) err = async_uv_await(req);
  uv_fs_free(req);
  return err;
}

static int async_fread(uv_file file, uv_buf_t* buf, int64_t offset, ssize_t* read ) {
  *read = 0;
  uv_fs_t* req = uv_fs_alloc();
  uv_loop_t* loop = async_uv_loop();
  int err = uv_fs_read(loop, req, file, buf, 1, offset, &async_fs_cb);
  if (err==0) {
    err = async_uv_await(req);
    if (err==0) *read = req->result;
  }
  uv_fs_free(req);
  return err;
}

/*-----------------------------------------------------------------
  Main
-----------------------------------------------------------------*/
static void test_stat() {
  const char* path = "cenv.h";
  uv_stat_t stat;
  int err = async_fstat(path,&stat);
  if (err==0) {
    printf("file %s last access time: %li\n", path, stat.st_atim.tv_sec);
  }
  else {
    fprintf(stderr, "stat error: %i: %s\n", err, uv_strerror(err));
  }
}

static char buffer[129];

static void test_fileread() {
  printf("opening file\n");
  uv_file f;
  int err = async_fopen("cenv.h", O_RDONLY, 0, &f);
  if (err == 0) {
    ssize_t read;
    ssize_t size = 0;
    uv_buf_t buf = uv_buf_init(buffer, 128);
    while ((err = async_fread(f, &buf, -1, &read)) == 0 && read > 0) {
      buffer[read-1] = 0;
      printf("reading %i bytes:\n%s\n", read, buf.base);
      size += read;
    }
    if (err == 0) {
      err = async_fclose(f);
      printf("closed file, total size: %li\n", size);
    }
  }
  if (err != 0) fprintf(stderr, "error: %i: %s\n", err, uv_strerror(err));
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

static void uv_main_cb(uv_timer_t* t_start) {
  // uv_mainx(t_start->loop);
  async_handler(t_start->loop, &uv_main_action, lh_value_null);
  uv_timer_stop(t_start);
}

int main() {
  uv_loop_t* loop = uv_default_loop();
  uv_timer_t t_start;
  uv_timer_init( loop, &t_start);
  uv_timer_start(&t_start, &uv_main_cb, 0, 0);
  printf("starting\n");
  int result = uv_run(loop, UV_RUN_DEFAULT);
  uv_loop_close(loop);

  char buf[128];
  printf("done! (press enter to quit)\n"); gets(buf);
  return ;
}
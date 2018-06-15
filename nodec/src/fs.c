#include <nodec.h>
#include <uv.h>
#include <assert.h>
#include <fcntl.h>




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
  uv_loop_t* loop = async_loop();
  int err = uv_fs_stat(loop, req, path, &_async_fs_cb);
  if (err == 0) {
    err = async_await_fs(req);
    if (err == 0) *stat = req->statbuf;
  }
  uv_fs_free(req);
  return err; // UV_UNKNOWN
}

uv_stat_t async_stat(const char* path) {
  uv_stat_t stat;
  _check_uv_errmsg(async_statx(path, &stat), path);
  return stat;
}

static int async_fstatx(uv_file file, uv_stat_t* stat) {
  memset(stat, 0, sizeof(uv_stat_t));
  uv_fs_t*   req = uv_fs_alloc();
  uv_loop_t* loop = async_loop();
  int err = uv_fs_fstat(loop, req, file, &_async_fs_cb);
  if (err == 0) {
    err = async_await_fs(req);
    if (err == 0) *stat = req->statbuf;
  }
  uv_fs_free(req);
  return err;
}

uv_stat_t async_fstat(uv_file file) {
  uv_stat_t stat;
  _check_uv_err(async_fstatx(file, &stat));
  return stat;
}

static int async_fopenx(const char* path, int flags, int mode, uv_file* file) {
  *file = -1;
  uv_fs_t* req = uv_fs_alloc();
  uv_loop_t* loop = async_loop();
  int err = uv_fs_open(loop, req, path, flags, mode, &_async_fs_cb);
  if (err == 0) {
    err = async_await_fs(req);
    if (err == 0) *file = (uv_file)(req->result);
  }
  uv_fs_free(req);
  return err;
}

uv_file async_fopen(const char* path, int flags, int mode) {
  uv_file file = -1;
  _check_uv_errmsg(async_fopenx(path, flags, mode, &file), path);
  return file;
}


static int async_fclosex(uv_file file) {
  if (file < 0) return 0;
  uv_fs_t*   req = uv_fs_alloc();
  uv_loop_t* loop = async_loop();
  int err = uv_fs_close(loop, req, file, &_async_fs_cb);
  if (err == 0) err = async_await_fs(req);
  uv_fs_free(req);
  return err;
}

void async_fclose(uv_file file) {
  _check_uv_err(async_fclosex(file));
}

static int async_freadx(uv_file file, uv_buf_t* buf, int64_t offset, ssize_t* read) {
  *read = 0;
  uv_fs_t* req = uv_fs_alloc();
  uv_loop_t* loop = async_loop();
  int err = uv_fs_read(loop, req, file, buf, 1, offset, &_async_fs_cb);
  if (err == 0) {
    err = async_await_fs(req);
    if (err == 0) *read = req->result;
  }
  uv_fs_free(req);
  return err;
}

ssize_t async_fread(uv_file file, uv_buf_t* buf, int64_t offset) {
  ssize_t read = 0;
  _check_uv_err(async_freadx(file, buf, offset, &read));
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
  _check_uv_errmsg(async_fread_fullx(path, &len, &contents), path);
  return contents;
}



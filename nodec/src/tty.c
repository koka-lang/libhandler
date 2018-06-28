/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "nodec.h"
#include "nodec-internal.h"
#include "nodec-primitive.h"
#include <assert.h>

static uv_stream_t* stream_of_tty(uv_tty_t* tty) {
  return (uv_stream_t*)tty;  // super class
}

typedef struct _tty_t {
  uv_tty_t*      _stdin;
  uv_tty_t*      _stdout;
  uv_tty_t*      _stderr;
  int            mode;
} tty_t;

static tty_t* nodec_tty_alloc() {
  return nodec_zero_alloc(tty_t);
}

lh_value _nodec_tty_allocv() {
  return lh_value_ptr(nodec_tty_alloc());
}


static void nodec_tty_free(tty_t* tty) {
  if (tty->_stdin != NULL) nodec_stream_free(stream_of_tty(tty->_stdin));
  if (tty->_stdout != NULL) nodec_stream_free(stream_of_tty(tty->_stdout));
  if (tty->_stderr != NULL) nodec_stream_free(stream_of_tty(tty->_stderr));
  // `read_stream_t* in` is owned by _stdin and freed by it
  nodec_free(tty);
}


void _nodec_tty_freev(lh_value ttyv) {
  uv_tty_reset_mode();
  nodec_tty_free((tty_t*)lh_ptr_value(ttyv));
}

implicit_define(tty)

static tty_t* tty_get() {
  return (tty_t*)lh_ptr_value(implicit_get(tty));
}

char* async_tty_readline() {
  tty_t* tty = tty_get();
  if (tty->_stdin == NULL) {
    tty->_stdin = nodec_zero_alloc(uv_tty_t);
    nodec_check(uv_tty_init(async_loop(), tty->_stdin, 0, 1));
    nodec_read_start(stream_of_tty(tty->_stdin), 0, 64, 64);
  }
  return async_read_line(stream_of_tty(tty->_stdin));
}

void async_tty_write(const char* s) {
  tty_t* tty = tty_get();
  if (tty->_stdout == NULL) {
    tty->_stdout = nodec_zero_alloc(uv_tty_t);
    nodec_check(uv_tty_init(async_loop(), tty->_stdout, 1, 0));
  }
  async_write(stream_of_tty(tty->_stdout), s);
}

// Flush any outstanding writes
void async_tty_shutdown() {
  tty_t* tty = tty_get();
  if (tty->_stdout == NULL) async_shutdown(stream_of_tty(tty->_stdout));
  if (tty->_stderr == NULL) async_shutdown(stream_of_tty(tty->_stderr));
}
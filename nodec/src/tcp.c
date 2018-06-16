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

void check_uv_err_addr(int err, const struct sockaddr_in* addr) {
  // todo: switch to ipv6 address if needed
  if (err != 0) {
    char buf[128];
    buf[0] = 0;
    if (addr != NULL) uv_ip4_name(addr, &buf, 127);
    buf[127] = 0;
    check_uv_errmsg(err, buf);
  }
}

void check_uv_err_addr6(int err, const struct sockaddr_in6* addr) {
  if (err != 0) {
    char buf[256];
    buf[0] = 0;
    if (addr != NULL) uv_ip6_name(addr, &buf, 255);
    buf[255] = 0;
    check_uv_errmsg(err, buf);
  }
}

/*-----------------------------------------------------------------
  handling tcp
-----------------------------------------------------------------*/
uv_tcp_t* nodec_tcp_alloc() {
  return nodec_alloc(uv_tcp_t);
}

void nodec_tcp_free(uv_tcp_t* tcp) {
  nodec_free(tcp);
}

void nodec_tcp_freev(lh_value tcp) {
  nodec_tcp_free(lh_ptr_value(tcp));
}

uv_tcp_t* nodec_tcp_init() {
  uv_tcp_t* tcp = nodec_tcp_alloc();
  check_uv_err( uv_tcp_init(async_loop(), tcp) );  
  return tcp;
}

void nodec_tcp_bind(uv_tcp_t* handle, const struct sockaddr* addr, unsigned int flags) {
  check_uv_err_addr(uv_tcp_bind(handle, addr, flags),addr);
}

static void _listen_cb(uv_stream_t* server, int status) {
  int err = 0;
  if (status != 0) {
    err = status;
  }
  else if (server==NULL) {
    err = UV_EINVAL;
  }
  else {
    uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    err = uv_tcp_init(server->loop, &client);
    if (err == 0) {
      err = uv_accept(server, client);
      if (err == 0) {
        channel_t* ch = (channel_t*)server->data;
        if (ch != NULL) {
          channel_elem elem = { lh_value_ptr(client),lh_value_null,0 };
          channel_emit(ch, elem); // use channelx_emit
        }
      }
    }
  }
  if (err!=0) {
    fprintf(stderr, "connection error: %i\n", err);
  }
}

channel_t* nodec_tcp_listen(uv_tcp_t* tcp, int backlog) {
  check_uv_err(uv_listen((uv_stream_t*)tcp, backlog, &_listen_cb));
  channel_t* ch = channel_alloc(); // todo: if this fails, should we close the connection?
  tcp->data = ch;
  return ch;
}
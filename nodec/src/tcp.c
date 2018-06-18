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
    if (addr != NULL) uv_ip4_name(addr, buf, 127);
    buf[127] = 0;
    check_uv_errmsg(err, buf);
  }
}

void check_uv_err_addr6(int err, const struct sockaddr_in6* addr) {
  if (err != 0) {
    char buf[256];
    buf[0] = 0;
    if (addr != NULL) uv_ip6_name(addr, buf, 255);
    buf[255] = 0;
    check_uv_errmsg(err, buf);
  }
}

/*-----------------------------------------------------------------
  handling tcp
-----------------------------------------------------------------*/


void nodec_tcp_free(uv_tcp_t* tcp) {
  nodec_stream_free((uv_stream_t*)tcp);
}

void nodec_tcp_freev(lh_value tcp) {
  nodec_tcp_free(lh_ptr_value(tcp));
}

uv_tcp_t* nodec_tcp_alloc() {
  uv_tcp_t* tcp = nodec_alloc(uv_tcp_t);
  check_uv_err( uv_tcp_init(async_loop(), tcp) );  
  return tcp;
}


void nodec_tcp_bind(uv_tcp_t* handle, const struct sockaddr_in* addr, unsigned int flags) {
  check_uv_err_addr(uv_tcp_bind(handle, (const struct sockaddr*)addr, flags), addr);
}

static void _listen_cb(uv_stream_t* server, int status) {
  fprintf(stderr, "connection came in!\n");
  uv_tcp_t* client = NULL;
  int err = 0;
  if (status != 0) {
    err = status;
  }
  else if (server==NULL) {
    err = UV_EINVAL;
  }
  else {
    client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    if (client==NULL) err = UV_ENOMEM;
    else {
      nodec_zero(uv_tcp_t, client);
      err = uv_tcp_init(server->loop, client);
    }
    if (err==0) {
      err = uv_accept(server, (uv_stream_t*)client);
      if (err == 0) {
        channel_t* ch = (channel_t*)server->data;
        if (ch!=NULL) {
          // here we emit into the channel
          // this will either queue the element, or call a listener
          // entering a listener is ok since that will be a resume 
          // under an async/try handler again.
          // TODO: we should have a size limited queue and check the emit return value
          channel_elem elem = { lh_value_ptr(client),lh_value_null,0 };
          fprintf(stderr, "emit\n");
          channel_emit(ch, elem);
        }
      }
    }
  }
  if (err!=0) {
    // deallocate client on error
    if (client!=NULL) {
      nodec_stream_free((uv_stream_t*)client);
      client = NULL;
    }
    fprintf(stderr, "connection error: %i\n", err);
  }
}

// Free TCP stream associated with a tcp channel
static void _channel_release_tcp(lh_value tcpv) {
  uv_tcp_t* tcp = (uv_tcp_t*)lh_ptr_value(tcpv);
  tcp->data = NULL; // is the channel itself; don't free it in the stream_free
  nodec_tcp_free(tcp);
}

tcp_channel_t* nodec_tcp_listen(uv_tcp_t* tcp, int backlog, bool channel_owns_tcp) {
  if (backlog <= 0) backlog = 512;
  check_uv_err(uv_listen((uv_stream_t*)tcp, backlog, &_listen_cb));
  tcp_channel_t* ch = (tcp_channel_t*)channel_alloc_ex(backlog,
                          (channel_owns_tcp ? &_channel_release_tcp : NULL), 
                              lh_value_ptr(tcp), NULL); 
  tcp->data = ch;
  return ch;
}

tcp_channel_t* nodec_tcp_listen_at4(const char* ip, int port, int backlog, unsigned int flags) {
  struct sockaddr_in addr;
  check_uv_err(uv_ip4_addr(ip, port, &addr));
  uv_tcp_t* tcp = nodec_tcp_alloc();
  tcp_channel_t* ch = NULL;
  {on_exn(nodec_tcp_freev, lh_value_ptr(tcp)) {
    nodec_tcp_bind(tcp, &addr, flags);
    ch = nodec_tcp_listen(tcp, backlog, true);
  }}
  return ch;
}

uv_stream_t* tcp_channel_receive(tcp_channel_t* ch) {
  channel_elem e = channel_receive(ch);
  //printf("got a connection!\n");
  return (uv_stream_t*)lh_ptr_value(e.data);
}

/*-----------------------------------------------------------------
  A TCP server
-----------------------------------------------------------------*/

typedef struct _tcp_serve_args {
  tcp_channel_t*      ch;
  nodec_tcp_servefun* serve;
} tcp_serve_args;

static lh_value tcp_servev(lh_value argsv) {
  static int ids = 0;
  int id = ids++;
  tcp_serve_args* args = (tcp_serve_args*)lh_ptr_value(argsv);
  tcp_channel_t* ch = args->ch;
  nodec_tcp_servefun* serve = args->serve;
  do {
    uv_stream_t* client = tcp_channel_receive(ch);
    {with_stream(client) {
      serve(id, client);
    }}
  } while (false);  // should be until termination
  return lh_value_null;
}

void nodec_tcp_server_at4(const char* ip, int port, int backlog, unsigned int flags, int n, nodec_tcp_servefun* servefun) {
  tcp_channel_t* ch = nodec_tcp_listen_at4(ip, port, backlog, flags);
  {with_tcp_channel(ch) {
    {with_alloc(tcp_serve_args, sargs) {
      sargs->ch = ch;
      sargs->serve = servefun;
      {with_nalloc(n, lh_actionfun*, actions) {
        {with_nalloc(n, lh_value, args) {
          for (int i = 0; i < n; i++) {
            actions[i] = &tcp_servev;
            args[i] = lh_value_any_ptr(sargs);
          }
          interleave(n, actions, args);
        }}
      }}
    }}
  }}
}


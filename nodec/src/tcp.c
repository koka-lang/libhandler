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

void check_uv_err_addr(int err, const struct sockaddr* addr) {
  // todo: switch to ipv6 address if needed
  if (err != 0) {
    char buf[256];
    buf[0] = 0;
    if (addr != NULL) {
      if (addr->sa_family == AF_INET6) {
        uv_ip6_name((const struct sockaddr_in6*)addr, buf, 255);
      }
      else {
        uv_ip4_name((const struct sockaddr_in*)addr, buf, 255);
      }
    }
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
  uv_tcp_t* tcp = nodec_zero_alloc(uv_tcp_t);
  check_uverr( uv_tcp_init(async_loop(), tcp) );  
  return tcp;
}


void nodec_tcp_bind(uv_tcp_t* handle, const struct sockaddr* addr, unsigned int flags) {
  check_uv_err_addr(uv_tcp_bind(handle, addr, flags), addr);
} 

static void _listen_cb(uv_stream_t* server, int status) {
  fprintf(stderr, "connection came in!\n");
  uv_tcp_t* client = NULL;
  channel_t* ch = NULL;
  int err = 0;
  if (status != 0) {
    err = status;
  }
  else if (server==NULL) {
    err = UV_EINVAL;
  }
  else if ((ch = (channel_t*)server->data) == NULL) {
    err = UV_EINVAL;
  }
  else if (channel_is_full(ch)) {
    err = UV_ENOSPC; // stop accepting connections if the queue is full
  }
  else {
    client = (uv_tcp_t*)calloc(1,sizeof(uv_tcp_t));
    if (client == NULL) {
      err = UV_ENOMEM;
    }
    else {
      err = uv_tcp_init(server->loop, client);
      if (err == 0) {
        err = uv_accept(server, (uv_stream_t*)client);
        if (err == 0) {
          // here we emit into the channel
          // this will either queue the element, or call a listener
          // entering a listener is ok since that will be a resume 
          // under an async/try handler again.
          // TODO: we should have a size limited queue and check the emit return value
          channel_elem elem = { lh_value_ptr(client),lh_value_null,0 };
          fprintf(stderr, "emit\n");
          err = channel_emit(ch, elem);  // if err==UV_NOSPC the channel was full
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
    fprintf(stderr, "connection error: %i: %s\n", err, uv_strerror(err));
  }
}

// Free TCP stream associated with a tcp channel
static void _channel_release_tcp(lh_value tcpv) {
  uv_tcp_t* tcp = (uv_tcp_t*)lh_ptr_value(tcpv);
  tcp->data = NULL; // is the channel itself; don't free it in the stream_free
  nodec_tcp_free(tcp);
}

static void _channel_release_client(channel_elem elem) {
  uv_stream_t* client = (uv_stream_t*)lh_ptr_value(elem.data);
  if (client != NULL) {
    nodec_stream_free(client);    
  }
}

tcp_channel_t* nodec_tcp_listen(uv_tcp_t* tcp, int backlog, bool channel_owns_tcp) {
  if (backlog <= 0) backlog = 512;
  check_uverr(uv_listen((uv_stream_t*)tcp, backlog, &_listen_cb));
  tcp_channel_t* ch = (tcp_channel_t*)channel_alloc_ex(8, // TODO: should be small?
                          (channel_owns_tcp ? &_channel_release_tcp : NULL), 
                              lh_value_ptr(tcp), &_channel_release_client );  
  tcp->data = ch;
  return ch;
}

void nodec_ip4_addr(const char* ip, int port, struct sockaddr_in* addr) {
  check_uverr(uv_ip4_addr(ip, port, addr));
}

void nodec_ip6_addr(const char* ip, int port, struct sockaddr_in6* addr) {
  check_uverr(uv_ip6_addr(ip, port, addr));
}

tcp_channel_t* nodec_tcp_listen_at(const struct sockaddr* addr, int backlog) {
  uv_tcp_t* tcp = nodec_tcp_alloc();
  tcp_channel_t* ch = NULL;
  {on_abort(nodec_tcp_freev, lh_value_ptr(tcp)) {
    nodec_tcp_bind(tcp, addr, 0);
    ch = nodec_tcp_listen(tcp, backlog, true);
  }}
  return ch;
}

uv_stream_t* async_tcp_channel_receive(tcp_channel_t* ch) {
  channel_elem e = channel_receive(ch);
  //printf("got a connection!\n");
  return (uv_stream_t*)lh_ptr_value(e.data);
}


/*-----------------------------------------------------------------
    A TCP server
-----------------------------------------------------------------*/

static lh_value async_log_tcp_exn(lh_value exnv) {
  lh_exception* exn = (lh_exception*)lh_ptr_value(exnv);
  if (exn == NULL || exn->data == NULL) return lh_value_null;
  fprintf(stderr, "tcp server error: %i: %s\n", exn->code, (exn->msg == NULL ? "unknown" : exn->msg));
  return lh_value_null;
}


typedef struct _tcp_serve_args {
  tcp_channel_t*      ch;
  uint64_t            timeout;
  nodec_tcp_servefun* serve;
  lh_actionfun*       on_exn;
} tcp_serve_args;

typedef struct _tcp_client_args {
  int           id;
  uint64_t      timeout;
  uv_stream_t*  client;
  nodec_tcp_servefun* serve;
} tcp_client_args;

static lh_value tcp_serve_client(lh_value argsv) {
  tcp_client_args* args = (tcp_client_args*)lh_ptr_value(argsv);
  args->serve(args->id, args->client);
  return lh_value_null;
}

static lh_value tcp_serve_timeout(lh_value argsv) {
  tcp_client_args* args = (tcp_client_args*)lh_ptr_value(argsv);
  if (args->timeout == 0) {
    return tcp_serve_client(argsv);
  }
  else {
    bool timedout = false;
    lh_value result = async_timeout(&tcp_serve_client, argsv, args->timeout, &timedout);
    if (timedout) lh_throw_http_err(408);
    return result;
  }
}


static lh_value tcp_servev(lh_value argsv) {
  static int ids = 0;
  int id = ids++;
  tcp_serve_args args = *((tcp_serve_args*)lh_ptr_value(argsv));  
  do {
    uv_stream_t* client = async_tcp_channel_receive(args.ch);
    {with_stream(client) {
      lh_exception* exn;
      tcp_client_args cargs = { id, args.timeout, client, args.serve };
      lh_try( &exn, &tcp_serve_timeout, lh_value_any_ptr(&cargs)); 
      if (exn != NULL) {
        // send an exception response
        // wrap in try itself in case writing gives an error too!
        lh_exception* wrap = lh_exception_alloc(exn->code, exn->msg);
        wrap->data = client;
        lh_exception* ignore_exn;
        lh_try(&ignore_exn, args.on_exn, lh_value_any_ptr(wrap));
        lh_exception_free(wrap);
        lh_exception_free(exn);
      }
    }}
  } while (false);  // should be until termination
  return lh_value_null;
}

void async_tcp_server_at(const struct sockaddr* addr, int backlog, int n, uint64_t timeout, 
                           nodec_tcp_servefun* servefun, lh_actionfun* on_exn) {
  tcp_channel_t* ch = nodec_tcp_listen_at(addr, backlog);
  {with_tcp_channel(ch) {
    {with_alloc(tcp_serve_args, sargs) {
      sargs->ch = ch;
      sargs->timeout = timeout;
      sargs->serve = servefun;
      sargs->on_exn = (on_exn == NULL ? &async_log_tcp_exn : on_exn);
      {with_alloc_n(n, lh_actionfun*, actions) {
        {with_alloc_n(n, lh_value, args) {
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


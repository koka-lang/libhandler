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
  uv_tcp_t* tcp = nodec_zalloc(uv_tcp_t);
  check_uv_err( uv_tcp_init(async_loop(), tcp) );  
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
  check_uv_err(uv_listen((uv_stream_t*)tcp, backlog, &_listen_cb));
  tcp_channel_t* ch = (tcp_channel_t*)channel_alloc_ex(8, // TODO: should be small?
                          (channel_owns_tcp ? &_channel_release_tcp : NULL), 
                              lh_value_ptr(tcp), &_channel_release_client );  
  tcp->data = ch;
  return ch;
}

void nodec_ip4_addr(const char* ip, int port, struct sockaddr_in* addr) {
  check_uv_err(uv_ip4_addr(ip, port, addr));
}

void nodec_ip6_addr(const char* ip, int port, struct sockaddr_in6* addr) {
  check_uv_err(uv_ip6_addr(ip, port, addr));
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
  HTTP errors|
-----------------------------------------------------------------*/
const char* http_error_headers =
"HTTP/1.1 %i %s\r\n"
"Server : NodeC\r\n"
"Content-Length : %i\r\n"
"Content-Type : text/html; charset=utf-8\r\n"
"Connection : Closed\r\n"
"\r\n";

const char* http_error_body =
"<!DOCTYPE html>"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"</head>\n"
"<body>\n"
"  <p>Error %i (%s)%s%s.</p>\n"
"</body>\n"
"</html>\n";

typedef int http_status;

typedef struct _http_err_reason {
  http_status status;
  const char* reason;
} http_err_reason;

static http_err_reason http_reasons[] = {
  {200, "OK" },
  {500, "Internal Server Error" },
  {400, "Bad Request" },
  {401, "Unauthorized" },
  {100, "Continue"},
  {101, "Switching Protocols"},
  {201, "Created"},
  {202, "Accepted"},
  {203, "Non - Authoritative Information"},
  {204, "No Content"},
  {205, "Reset Content" },
  {206, "Partial Content" },
  {300, "Multiple Choices" },
  {301, "Moved Permanently"},
  {302, "Found"},
  {303, "See Other"},
  {304, "Not Modified"},
  {305, "Use Proxy"},
  {307, "Temporary Redirect"},
  {402, "Payment Required"},
  {403, "Forbidden"},
  {404, "Not Found"},
  {405, "Method Not Allowed"},
  {406, "Not Acceptable"},
  {407, "Proxy Authentication Required"},
  {408, "Request Time - out"},
  {409, "Conflict"},
  {410, "Gone"},
  {411, "Length Required"},
  {412, "Precondition Failed"},
  {413, "Request Entity Too Large"},
  {414, "Request - URI Too Large"},
  {415, "Unsupported Media Type"},
  {416, "Requested range not satisfiable"},
  {417, "Expectation Failed"},
  {501, "Not Implemented"},
  {502, "Bad Gateway"},
  {503, "Service Unavailable"},
  {504, "Gateway Time - out"},
  {505, "HTTP Version not supported"},
  {-1, NULL }
};

static void async_write_http_err(uv_stream_t* client, http_status code, const char* msg) {
  const char* reason = "Unknown";
  for (http_err_reason* r = http_reasons; r->reason != NULL; r++) {
    if (r->status == code) {
      reason = r->reason;
      break;
    }
  }
  char body[256];
  snprintf(body, 255, http_error_body, code, reason, (msg == NULL ? "" : ": "), (msg == NULL ? "" : msg));
  body[255] = 0;
  char headers[256];
  snprintf(headers, 255, http_error_headers, code, reason, strlen(body));
  headers[255] = 0;
  const char* strs[2] = { headers, body };
  fprintf(stderr, "ERROR: %i (%s): %s\n\n", code, reason, (msg == NULL ? "" : msg));
  async_write_strs(client, strs, 2 );
}

static lh_value async_write_http_exnv(lh_value exnv) {
  lh_exception* exn = (lh_exception*)lh_ptr_value(exnv);
  if (exn == NULL || exn->data==NULL) return lh_value_null;
  uv_stream_t* client = exn->data;
  async_write_http_err(client, 500, exn->msg);
  return lh_value_null;
}

/*-----------------------------------------------------------------
    A TCP/HTTP server
-----------------------------------------------------------------*/

typedef struct _http_serve_args {
  tcp_channel_t*      ch;
  uint64_t            timeout;
  nodec_tcp_servefun* serve;
} http_serve_args;

typedef struct _http_client_args {
  int           id;
  uint64_t      timeout;
  uv_stream_t*  client;
  nodec_tcp_servefun* serve;
} http_client_args;

static lh_value http_serve_client(lh_value argsv) {
  http_client_args* args = (http_client_args*)lh_ptr_value(argsv);
  args->serve(args->id, args->client);
  return lh_value_null;
}

static lh_value http_serve_timeout(lh_value argsv) {
  http_client_args* args = (http_client_args*)lh_ptr_value(argsv);
  bool timedout = false;
  async_timeout(&http_serve_client, argsv, args->timeout, &timedout);
  if (timedout) lh_throw_str(408, "request time-out");
  return lh_value_null;
}

static lh_value http_servev(lh_value argsv) {
  static int ids = 0;
  int id = ids++;
  http_serve_args args = *((http_serve_args*)lh_ptr_value(argsv));  
  do {
    uv_stream_t* client = async_tcp_channel_receive(args.ch);
    {with_stream(client) {
      lh_exception* exn;
      http_client_args cargs = { id, args.timeout, client, args.serve };
      lh_try( &exn, &http_serve_timeout, lh_value_any_ptr(&cargs)); //ignore timeout for now
      if (exn != NULL) {
        // send an exception response
        // wrap in try itself in case writing gives an error too!
        lh_exception* wrap = lh_exception_alloc(exn->code, exn->msg);
        wrap->data = client;
        lh_exception* ignore_exn;
        lh_try(&ignore_exn, &async_write_http_exnv, lh_value_any_ptr(wrap));
        lh_exception_free(wrap);
        lh_exception_free(exn);
      }
    }}
  } while (false);  // should be until termination
  return lh_value_null;
}

void async_http_server_at(const struct sockaddr* addr, int backlog, int n, uint64_t timeout, nodec_tcp_servefun* servefun) {
  tcp_channel_t* ch = nodec_tcp_listen_at(addr, backlog);
  {with_tcp_channel(ch) {
    {with_alloc(http_serve_args, sargs) {
      sargs->ch = ch;
      sargs->timeout = (timeout==0 ? 30000 : timeout);
      sargs->serve = servefun;
      {with_nalloc(n, lh_actionfun*, actions) {
        {with_nalloc(n, lh_value, args) {
          for (int i = 0; i < n; i++) {
            actions[i] = &http_servev;
            args[i] = lh_value_any_ptr(sargs);
          }
          interleave(n, actions, args);
        }}
      }}
    }}
  }}
}


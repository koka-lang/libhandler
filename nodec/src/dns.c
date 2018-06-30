/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "nodec.h"
#include "nodec-primitive.h"
#include "nodec-internal.h"
#include <assert.h>

static void addrinfo_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
  async_req_resume((uv_req_t*)req, status >= 0 ? 0 : status);
}

struct addrinfo* async_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints) {
  struct addrinfo* info = NULL;
  {with_req(uv_getaddrinfo_t,req) {
    nodec_check(uv_getaddrinfo(async_loop(), req, &addrinfo_cb, node, service, hints));
    async_await_once((uv_req_t*)req);
    info = req->addrinfo;
  }}
  return info;
}

void nodec_free_addrinfo(struct addrinfo* info) {
  if (info != NULL) uv_freeaddrinfo(info);
}

void nodec_free_addrinfov(lh_value infov) {
  nodec_free_addrinfo((struct addrinfo*)lh_ptr_value(infov));
}

static void nameinfo_cb(uv_getnameinfo_t* req, int status, const char* hostname, const char* service) {
  async_req_resume((uv_req_t*)req, status >= 0 ? 0 : status);
}

void async_getnameinfo(const struct sockaddr* addr, int flags, char** node, char** service) {
  if (node != NULL) *node = NULL;
  if (service != NULL) *service = NULL;
  {with_req(uv_getnameinfo_t, req) {
    nodec_check(uv_getnameinfo(async_loop(), req, &nameinfo_cb, addr, flags));
    async_await_once((uv_req_t*)req);
    if (node != NULL) *node = nodec_strndup(req->host, NI_MAXHOST);
    if (service != NULL) *service = nodec_strndup(req->service, NI_MAXSERV);
  }}
}
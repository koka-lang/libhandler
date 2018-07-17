#include <stdio.h>
#include <nodec.h>
#include <nodec-primitive.h>
#include <http_parser.h>
#include "request.h"

const char* response_headers;
const char* response_body;

/*-----------------------------------------------------------------------------
    header_callback_t
-----------------------------------------------------------------------------*/
typedef struct _header_callback_data_t {
  size_t count;
} header_callback_data_t;

/*-----------------------------------------------------------------------------
   header_callback
-----------------------------------------------------------------------------*/
static void header_callback(const header_t* header, void* data) {
  header_callback_data_t* callback_data = (header_callback_data_t*)data;
  printf("%zu: { \"%s\", \"%s\" }\n",
    callback_data->count, header->field.s, header->value.s);
  callback_data->count += 1;
}

/*-----------------------------------------------------------------------------
   process_completed_request
-----------------------------------------------------------------------------*/
static void process_completed_request(http_request_t* req) {

  const char* method_names[] = {
#define T(num, name, string) #string,
        HTTP_METHOD_MAP(T)
#undef T
  };

  printf("\n\n--------------------------------"
    "--------------------------------------------------\n");
  printf("http_major: %u\n", http_request_http_major(req));
  printf("http_minor: %u\n", http_request_http_minor(req));
  printf("content_length: %llu\n", http_request_content_length(req));
  const enum http_method method = http_request_method(req);
  printf("method: %d (%s)\n", method, method_names[method]);
  string_t url = http_request_url(req);
  if (url.s != 0 && url.len > 0)
    printf("url: \"%s\"\n", url.s);
  printf("\n");

  header_callback_data_t header_callback_data = { 0 };
  http_request_iter_headers(req, header_callback, &header_callback_data);

  printf("-------------------------------------------"
    "---------------------------------------\n\n");
}

/*-----------------------------------------------------------------------------
   process_request
-----------------------------------------------------------------------------*/
static void process_request(http_request_t* const Req, uv_buf_t* Buffer) {
  extern bool debug_verbose;
  debug_verbose = false;
  http_request_execute(Req, Buffer->base, Buffer->len);
  if (http_request_headers_are_complete(Req))
    process_completed_request(Req);
}

/*-----------------------------------------------------------------------------
   http_request_freev
-----------------------------------------------------------------------------*/
static void http_request_freev(lh_value req) {
  http_request_free(lh_ptr_value(req));
}

/*-----------------------------------------------------------------------------
   process_request_buf
-----------------------------------------------------------------------------*/
static void process_request_buf(uv_buf_t* buf) {
  http_request_t* const req = http_request_alloc();
  {defer(http_request_freev, lh_value_ptr(req)) {
    process_request(req, buf);
  }}
}




#include <stdio.h>
#include <nodec.h>
#include <nodec-primitive.h>
#include <http_parser.h>
#include "request.h"

const char* response_headers;
const char* response_body;

/*-----------------------------------------------------------------
Test HTTP
-----------------------------------------------------------------*/

//int bVerbose = 1;
//
//void init_settings(struct http_parser_settings* settings);
//
//static const char* http_type_str(enum http_parser_type type)
//{
//	switch (type) {
//	case HTTP_REQUEST: return "HTTP_REQUEST";
//	case HTTP_RESPONSE: return "HTTP_RESPONSE";
//	case HTTP_BOTH: return "HTTP_BOTH";
//	default: return "UNKNOWN";
//	}
//}
//
//static int test_chunk(struct http_parser *parser, const struct http_parser_settings *settings, const char* chunk) {
//	size_t const len0 = strlen(chunk);
//	size_t const len1 = HTTP_PARSER_EXECUTE(parser, settings, chunk, len0);
//	if (parser->http_errno != HPE_OK || len0 != len1) {
//		printf("Parsing Error\n");
//		return 1;
//	}
//	else
//		return 0;
//}
//
//static void test_chunks(const char* chunks[], size_t nchunks, enum http_parser_type type) {
//	size_t i;
//	struct http_parser_settings settings;
//	struct http_parser parser;
//
//	init_settings(&settings);
//	HTTP_PARSER_INIT(&parser, type);
//	for (i = 0; i < nchunks; i++) {
//		if (test_chunk(&parser, &settings, chunks[i]) != 0)
//			break;
//	}
//	printf("  type: %s\n", http_type_str(parser.type));
//	if (parser.type == HTTP_REQUEST)
//		printf("  method: %s\n", http_method_str(parser.method));
//}
//

/*---------------------------------------------------------------------------*\
*   header_callback_t                                                         *
\*---------------------------------------------------------------------------*/
typedef struct _header_callback_data_t {
    size_t count;
} header_callback_data_t;

/*---------------------------------------------------------------------------*\
*  header_callback                                                            *
\*---------------------------------------------------------------------------*/
static void header_callback(const header_t* header, void* data) {
    header_callback_data_t* callback_data = (header_callback_data_t*)data;
    printf("%zu: { \"%s\", \"%s\" }\n",
        callback_data->count, header->field.s, header->value.s);
    callback_data->count += 1;
}

/*---------------------------------------------------------------------------*\
*  process_completed_request                                                  *
\*---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*\
*  process_request                                                            *
\*---------------------------------------------------------------------------*/
static void process_request(http_request_t* const pR, uv_buf_t* pB) {
    http_request_execute(pR, pB->base, pB->len);
    if (http_request_headers_are_complete(pR))
        process_completed_request(pR);
}

/*---------------------------------------------------------------------------*\
*  http_request_freev                                                         *
\*---------------------------------------------------------------------------*/
static void http_request_freev(lh_value req) {
    http_request_free(lh_ptr_value(req));
}

/*---------------------------------------------------------------------------*\
*  process_request_buf                                                        *
\*---------------------------------------------------------------------------*/
static void process_request_buf(uv_buf_t* buf) {
    http_request_t* const req = http_request_alloc();
    {defer(http_request_freev, lh_value_ptr(req)) {
        process_request(req, buf);
    }}
}

/*---------------------------------------------------------------------------*\
*  test_httpx_serve                                                           *
\*---------------------------------------------------------------------------*/
static void test_httpx_serve(int strand_id, uv_stream_t* client) {
    fprintf(stderr, "strand %i entered\n", strand_id);
    // input
    uv_buf_t buf = async_read_buf(client);
    if (buf.len > 0) {
        {with_free(buf.base) {
            buf.base[buf.len] = 0;
            printf(
                "strand %i received:%u bytes\n%s\n",
                strand_id,
                buf.len,
                buf.base
            );
            process_request_buf(&buf);
        }}
    }
    // work
    printf("waiting %i secs...\n", 2);
    async_wait(1000 + strand_id * 1000);
    //check_uverr(UV_EADDRINUSE);

    // response
    {with_alloc_n(128, char, content_len) {
        snprintf(content_len, 128, "Content-Length: %zi\r\n\r\n", strlen(response_body));
        printf("strand %i: response body is %zi bytes\n", strand_id, strlen(response_body));
        const char* response[3] = { response_headers, content_len, response_body };
        async_write_strs(client, response, 3);
    }}
    printf("request handled\n\n\n");
}

/*---------------------------------------------------------------------------*\
*  process_completed_request                                                  *
\*---------------------------------------------------------------------------*/
void test_http() {
    define_ip4_addr("127.0.0.1", 8080, addr);
    async_http_server_at(addr, 0, 3, 0, &test_httpx_serve);
}


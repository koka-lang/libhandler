#ifndef callbacks_h
#define callbacks_h
#ifdef __cplusplus
extern "C" {
#endif

#include "http_parser.h"

	int on_message_begin(http_parser* parser);
	int on_url(http_parser* parser, const char *at, size_t length);
	int on_status(http_parser* parser, const char *at, size_t length);
	int on_header_field(http_parser* parser, const char *at, size_t length);
	int on_header_value(http_parser* parser, const char *at, size_t length);
	int on_headers_complete(http_parser* parser);
	int on_body(http_parser* parser, const char *at, size_t length);
	int on_message_complete(http_parser* parser);
	int on_chunk_header(http_parser* parser);
	int on_chunk_complete(http_parser* parser);

	void init_settings(struct http_parser_settings* settings);

#ifdef __cplusplus
}
#endif
#endif


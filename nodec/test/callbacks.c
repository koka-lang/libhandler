#include "stdio.h"
#include "callbacks.h"
#include "http_parser_wrapper.h"
#include "hexdump.h"
#include <memory.h>

extern int bVerbose;

static void
print_parser_only(const char* name, const http_parser *p) {
	if (bVerbose) {
		printf("\n%s:\n", name);
		printf("  http_parser: %p\n", p);
	}
}

static void
print_all(const char* name, const http_parser *p, const char *buf, size_t len) {
	if (bVerbose) {
		print_parser_only(name, p);
		printf("  buf: %p\n", buf);
		printf("  len: %zu\n", len);
		if (buf != 0 && len > 0)
			hexDump(buf, len);
	}
}

static int on_message_begin(http_parser* parser) {
	print_parser_only("on_message_begin", parser);
	return 0;
}

static int on_url(http_parser* parser, const char *at, size_t length) {
	print_all("on_url", parser, at, length);
	return 0;
}

static int on_status(http_parser* parser, const char *at, size_t length) {
	print_all("on_status", parser, at, length);
	return 0;
}

static int on_header_field(http_parser* parser, const char *at, size_t length) {
	print_all("on_header_field", parser, at, length);
	return 0;
}

static int on_header_value(http_parser* parser, const char *at, size_t length) {
	print_all("on_header_value", parser, at, length);
	return 0;
}

static int on_headers_complete(http_parser* parser) {
	print_parser_only("on_headers_complete", parser);
	int ans = HTTP_SHOULD_KEEP_ALIVE(parser);
	return 0;
}

static int on_body(http_parser* parser, const char *at, size_t length) {
	print_all("on_body", parser, at, length);
	int const ans = HTTP_BODY_IS_FINAL(parser);
	return 0;
}

static int on_message_complete(http_parser* parser) {
	print_parser_only("on_message_complete", parser);
	int ans = HTTP_SHOULD_KEEP_ALIVE(parser);
	return 0;
}

static int on_chunk_header(http_parser* parser) {
	print_parser_only("on_header", parser);
	return 0;
}

static int on_chunk_complete(http_parser* parser) {
	print_parser_only("on_chunk_complete", parser);
	return 0;
}

void init_settings(struct http_parser_settings* settings) {
	memset(settings, 0, sizeof(*settings));
	settings->on_message_begin = on_message_begin;
	settings->on_url = on_url;
	settings->on_status = on_status;
	settings->on_header_field = on_header_field;
	settings->on_header_value = on_header_value;
	settings->on_headers_complete = on_headers_complete;
	settings->on_body = on_body;
	settings->on_message_complete = on_message_complete;
	settings->on_chunk_header = on_chunk_header;
	settings->on_chunk_complete = on_chunk_complete;
}

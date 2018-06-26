#include "stdio.h"
#include "http_parser_wrapper.h"
#include "hexdump.h"

extern int bVerbose;

void debug_http_parser_init(http_parser* _parser, enum http_parser_type _type) {
	if (bVerbose) {
		printf("\nhttp_parser_init\n");
		printf("  parser: %p\n", _parser);
		printf("  type:   %d\n", _type);
	}
	http_parser_init(_parser, _type);
}

size_t debug_http_parser_execute(http_parser* _parser, const http_parser_settings* _settings, const char* _data, size_t _len) {

	if (bVerbose) {
		printf("\nhttp_parser_execute:\n");
		printf("  parser: %p\n", _parser);
		printf("  settings: %p\n", _settings);
		printf("  data: %p\n", _data);
		printf("  len: %zu\n", _len);
		if (_data != 0 && _len > 0)
			hexDump(_data, _len);
	}
	const size_t ans = http_parser_execute(_parser, _settings, _data, _len);
	if (bVerbose) {
		printf("\nhttp_parser_execute -> %zu\n", ans);
		printf("  error: %d (%s)\n", _parser->http_errno, http_errno_description(_parser->http_errno));
	}
	return ans;
}

int debug_http_should_keep_alive(const http_parser* _parser)
{
	if (bVerbose) {
		printf("\nhttp_should_keep_alive:\n");
		printf("  parser: %p\n", _parser);
	}
	const int ans = http_should_keep_alive(_parser);
	if (bVerbose)
		printf("http_should_keep_alive -> %d\n", ans);
	return ans;
}

void debug_http_parser_pause(http_parser* _parser, int _paused) {
	if (bVerbose) {
		printf("\nhttp_parser_pause:\n");
		printf("  parser: %p\n", _parser);
		printf("  paused: %d\n", _paused);
	}
	http_parser_pause(_parser, _paused);
}


int debug_http_body_is_final(const http_parser* _parser) {
	if (bVerbose) {
		printf("\nhttp_body_is_final:\n");
		printf("  parser: %p\n", _parser);
	}
	const int ans = http_body_is_final(_parser);
	if (bVerbose)
		printf("http_body_is_final -> %d\n", ans);
	return ans;
}
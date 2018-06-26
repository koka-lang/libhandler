#ifndef http_parser_wrapper_h
#define http_parser_wrapper_h
#ifdef __cplusplus
extern "C" {
#endif
#include "http_parser.h"

	void debug_http_parser_init(http_parser* _parser, enum http_parser_type _type);
	size_t debug_http_parser_execute(http_parser* _parser, const http_parser_settings* _settings, const char* _data, size_t _len);
	int debug_http_should_keep_alive(const http_parser* _parser);
	void debug_http_parser_pause(http_parser* _parser, int _paused);
	int debug_http_body_is_final(const http_parser* _parser);

#ifdef VERBOSE
#define HTTP_PARSER_EXECUTE debug_http_parser_execute
#define HTTP_SHOULD_KEEP_ALIVE debug_http_should_keep_alive
#define HTTP_PARSER_PAUSE debug_http_parser_pause
#define HTTP_PARSER_INIT debug_http_parser_init
#define HTTP_BODY_IS_FINAL debug_http_body_is_final
#else
#define HTTP_PARSER_EXECUTE http_parser_execute
#define HTTP_SHOULD_KEEP_ALIVE http_should_keep_alive
#define HTTP_PARSER_PAUSE http_parser_pause
#define HTTP_PARSER_INIT http_parser_init
#define HTTP_BODY_IS_FINAL http_body_is_final
#endif

#ifdef __cplusplus
}
#endif
#endif

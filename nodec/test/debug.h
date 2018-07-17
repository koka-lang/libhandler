#pragma once
#include <stdlib.h>
#include <stdbool.h>
#include "http_parser.h"

/*-----------------------------------------------------------------------------
    exports
-----------------------------------------------------------------------------*/
void debug_free(void* block);
void* debug_calloc(size_t num, size_t size);
void* debug_realloc(void* block, size_t size);
void debug_memcpy(void* dst, const void* src, size_t size);
void hexDump(const void *addr, size_t len, const char* prefix);
void pause(const char* msg);
void print_parser_only(const char* name, const http_parser *p);
void print_all(const char* name, const http_parser *p, const char *buf, size_t len);
bool debug_verbose;
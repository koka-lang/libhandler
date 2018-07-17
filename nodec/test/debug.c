#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <memory.h>
#include <nodec.h>
#include "debug.h"

bool debug_verbose = true;

/*-----------------------------------------------------------------------------
   debug_free

    A debug version of free
-----------------------------------------------------------------------------*/
void debug_free(void* block) {
  if (debug_verbose)
    printf("free(%p) -> void\n", block);
  nodec_free(block);
}

/*-----------------------------------------------------------------------------
   debug_calloc

    A debug version of calloc
-----------------------------------------------------------------------------*/
void* debug_calloc(size_t num, size_t size) {
  void* ans = nodec_calloc(num, size);
  if (debug_verbose)
    printf("calloc(%zu, %zu) -> %p\n", num, size, ans);
  return ans;
}

/*-----------------------------------------------------------------------------
   debug_realloc

    A debug version of realloc
-----------------------------------------------------------------------------*/
void* debug_realloc(void* block, size_t size) {
  void* ans = nodec_realloc(block, size);
  if (debug_verbose)
    printf("realloc(%p, %zu) -> %p\n", block, size, ans);
  return ans;
}

/*-----------------------------------------------------------------------------
   hexDump

    Prints a hex representation of a block of memory
-----------------------------------------------------------------------------*/
void hexDump(const void *addr, size_t len, const char* prefix) {
  if (prefix)
    printf("%s\n", prefix);
  size_t i;
  unsigned char buff[17];
  unsigned char* const pc = (unsigned char*)addr;
  for (i = 0; i < len; i++) {
    if ((i % 16) == 0) {
      if (i != 0)
        printf("  %s\n", buff);
      printf("%p ", pc + i);
    }
    unsigned char c = pc[i];
    printf(" %02x", c);
    size_t const imod = i % 16;
    buff[imod] = isprint(c) ? c : '.';
    buff[imod + 1] = 0;
  }
  while ((i % 16) != 0) {
    printf("   ");
    i++;
  }
  printf("  %s\n", buff);
}

/*-----------------------------------------------------------------------------
   debug_memcpy

    A debug version of memcpy
-----------------------------------------------------------------------------*/
void debug_memcpy(void* dst, const void* src, size_t size) {
  if (debug_verbose)
    printf("debug_memcpy(%p, %p, %zu)\n", dst, src, size);
  memcpy(dst, src, size);
}

/*-----------------------------------------------------------------------------
   pause

    Print a message and then wait for the user to press ENTER
-----------------------------------------------------------------------------*/
void pause(const char* msg) {
  printf("%s\n", msg);
  char buf[16];
  gets_s(buf, sizeof(buf));
}

/*-----------------------------------------------------------------------------
    print_parser_only

        used for debugging http_parser callbacks
-----------------------------------------------------------------------------*/
void print_parser_only(const char* name, const http_parser *p) {
  if (debug_verbose) {
    printf("\n%s:\n", name);
    printf("  http_parser: %p\n", p);
  }
}

/*-----------------------------------------------------------------------------
    print_all

        used for debugging http_parser callbacks
-----------------------------------------------------------------------------*/
void print_all(const char* name, const http_parser *p, const char *buf, size_t len) {
  if (debug_verbose) {
    print_parser_only(name, p);
    printf("  buf: %p\n", buf);
    printf("  len: %zu\n", len);
    if (buf != 0 && len > 0)
      hexDump(buf, len, "Memory");
  }
}

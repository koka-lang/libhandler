#pragma once
#include <stdlib.h>

/*-----------------------------------------------------------------------------
    sbuf_t

        a buffer for storing a set of strings. Each stored string is
        is guaranteed to be followed by a null terminator
-----------------------------------------------------------------------------*/
typedef struct _sbuf_t {
  size_t total;   // number of char's allocated must have used < total
  size_t used;    // number of chars used = starting offset for next char string
  size_t start;   // start of current string
  char* buffer;   // buffer of chars (size = total)
} sbuf_t;

/*-----------------------------------------------------------------------------
    exports
-----------------------------------------------------------------------------*/
size_t sbuf_add(sbuf_t *self, const char* s, size_t len, size_t buf_inc);
void sbuf_append(sbuf_t *self, const char* s, size_t len, size_t buf_inc);
size_t sbuf_get_string_length(const sbuf_t* self);
const char* sbuf_get_string(const sbuf_t* self, size_t start);
void sbuf_init(sbuf_t* self);
void sbuf_delete(sbuf_t* self);

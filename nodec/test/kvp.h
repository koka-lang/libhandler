#pragma once
#include <stdlib.h>
#include <stdbool.h>

/*-----------------------------------------------------------------------------
    pascal_string_t

    Pascal type string where we store a charater reference and length of a
    string. In this case the reference is the staring position within
    a buffer of characters. The length does not include a terminator
-----------------------------------------------------------------------------*/
typedef struct _pascal_string_t {
  size_t start;
  size_t length;
} pascal_string_t;

/*-----------------------------------------------------------------------------
    kvp_t

    a pair of string-offset-length pairs representing a key and a value
-----------------------------------------------------------------------------*/
typedef struct _kvp_t {
  pascal_string_t key;
  pascal_string_t value;
} kvp_t;

/*-----------------------------------------------------------------------------
    kvpbuf_t

    A collection of key-value pairs stored in an array of kvp_t structures.
    total: total number kvp_t strutures allocted
    used: number of kvp_t structures are used. The used structures
    start from the start of the array and are consecutive
    buffer: a pointer to the array of kvp_t structures
-----------------------------------------------------------------------------*/
typedef struct _kvpbuf_t {
  size_t total;
  size_t used;
  kvp_t* buffer;
} kvpbuf_t;

/*-----------------------------------------------------------------------------
    exports
-----------------------------------------------------------------------------*/
void kvpbuf_add(kvpbuf_t* self, const kvp_t* kvp, size_t kvp_inc);
void kvpbuf_delete(kvpbuf_t* self);
void kvpbuf_init(kvpbuf_t* self);

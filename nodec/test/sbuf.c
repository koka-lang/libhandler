#include <memory.h>
#include "debug.h"
#include "utils.h"
#include "sbuf.h"

/*****************************************************************************\
*   sbuf_append                                                               *
*                                                                             *
*       append a new part to the current string in the string buffer.         *
*       Note that sbuf_add must have been called before this function can     *
*       be called.                                                            *
\*****************************************************************************/
void sbuf_append(sbuf_t *sbuf, const char* s, size_t len, size_t buf_inc)
{
    size_t total_needed = sbuf->used + len + 1;
    if (total_needed > sbuf->total) {
        sbuf->total = roundup(total_needed, buf_inc);
        sbuf->buffer = debug_realloc(sbuf->buffer, sbuf->total);
    }
    debug_memcpy(sbuf->buffer + sbuf->used, s, len);
    // this does not mark the teminator as used so it can be overwritten
    // by the next call to append
    sbuf->used += len;
    sbuf->buffer[sbuf->used] = 0;
}

/*****************************************************************************\
*   sbuf_add                                                                  *
*                                                                             *
*       adds a new string to the string buffer. The return value is the       *
*       offset of the string from the start of the buffer                     *
\*****************************************************************************/
size_t sbuf_add(sbuf_t *sbuf, const char* s, size_t len, size_t buf_inc)
{
    size_t start = 0;
    if (sbuf->used > 0) {
        start = sbuf->used + 1;
        // this marks the terminator as used and can never be overwritten again
        sbuf->used += 1;
    }
    sbuf_append(sbuf, s, len, buf_inc);
    sbuf->start = start;
    return start;
}

/*****************************************************************************\
*   sbuf_get_strinmg_length                                                   *
*                                                                             *
*       returns the length of the current string                              *
\*****************************************************************************/
size_t sbuf_get_string_length(const sbuf_t* self)
{
    return self->used - self->start;
}

/*****************************************************************************\
*   sbuf_get_string                                                           *
*                                                                             *
*       returns a pointer to the start of the string                          *
\*****************************************************************************/
const char* sbuf_get_string(const sbuf_t* self, size_t start)
{
    return self->buffer + start;
}

/*****************************************************************************\
*   sbuf_init                                                                 *
*                                                                             *
*       initiaizes the structure                                              *
\*****************************************************************************/
void sbuf_init(sbuf_t* self)
{
    memset(self, 0, sizeof(*self));
}

/*****************************************************************************\
*   sbuf_delete                                                               *
*                                                                             *
*       Releases all resources                                                *
\*****************************************************************************/
void sbuf_delete(sbuf_t* self)
{
    if (self->buffer) {
        debug_free(self->buffer);
        self->buffer = 0;
    }
}

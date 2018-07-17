#include <memory.h>
#include "debug.h"
#include "utils.h"
#include "kvp.h"

/*-----------------------------------------------------------------------------
    kvpbuf_add

    Add a key-value pair to the key-value-pair buffer
    kvp_inc: chunking size of key-value pairs to be added if necessary
    A copy of the key-value pair is made so the caller can released the
    memory after the call if so wished
-----------------------------------------------------------------------------*/
void kvpbuf_add(kvpbuf_t* self, const kvp_t* kvp, size_t kvp_inc) {
  if (self->total <= self->used) {
    size_t total = self->total + kvp_inc;
    self->buffer = debug_realloc(self->buffer, sizeof(kvp_t) * total);
    self->total = total;
  }
  self->buffer[self->used] = *kvp;
  self->used += 1;
}

/*-----------------------------------------------------------------------------
    kvpbuf_delete

    Releases all resources
-----------------------------------------------------------------------------*/
void kvpbuf_delete(kvpbuf_t* self) {
  if (self->buffer) {
    debug_free(self->buffer);
    self->buffer = 0;
  }
}

/*-----------------------------------------------------------------------------
    kvpbuf_init

    initializes all resources
-----------------------------------------------------------------------------*/
void kvpbuf_init(kvpbuf_t* self) {
  memset(self, 0, sizeof(self));
}

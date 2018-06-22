#include "nodec.h"
#include "nodec-internal.h"
#include <uv.h>
#include <assert.h> 

/*-----------------------------------------------------------------
  Wrappers for malloc
-----------------------------------------------------------------*/
// Set up different allocation functions
static lh_mallocfun*  custom_malloc = NULL;
static lh_callocfun*  custom_calloc = NULL;
static lh_reallocfun* custom_realloc = NULL;
static lh_freefun*    custom_free = NULL;

void nodec_register_malloc(lh_mallocfun* _malloc, lh_callocfun* _calloc, lh_reallocfun* _realloc, lh_freefun* _free) {
  lh_register_malloc(_malloc, _calloc, _realloc, _free);
  custom_malloc = _malloc;
  custom_calloc = _calloc;
  custom_realloc = _realloc;
  custom_free = _free;
}

void  nodec_freev(lh_value p) {
  nodec_free(lh_ptr_value(p));
}

void* _nodecx_malloc(size_t size) {
  return (custom_malloc==NULL ? malloc(size) : custom_malloc(size));
}

void* _nodecx_calloc(size_t count, size_t size) {
  return (custom_calloc == NULL ? calloc(count, size) : custom_calloc(count, size));
}

void* _nodecx_realloc(void* p, size_t newsize) {
  return (custom_realloc == NULL ? realloc(p,newsize) : custom_realloc(p,newsize));
}

void  _nodec_free(void* p) {
  if (p != NULL) {
    if (custom_free == NULL) free(p); else custom_free(p);
  }
}


void* check_nonnull(void* p) {
  if (p == NULL) check_uverr(UV_ENOMEM);
  return p;
}

void* _nodec_malloc(size_t size) {
  return check_nonnull(_nodecx_malloc(size));
}

void* _nodec_calloc(size_t count, size_t size) {
  return check_nonnull(_nodecx_calloc(count,size));
}

void* _nodec_realloc(void* p, size_t newsize) {
  return check_nonnull(_nodecx_realloc(p,newsize));
}

static char* _nodec_strndup(const char* s, size_t max) {
  size_t n = (max == SIZE_MAX ? max : max + 1);
  char* t = nodec_alloc_n(n,char);
  #ifdef _MSC_VER
  strncpy_s(t, n, s, max);
  #else
  strncpy(t, s, max);
  #endif
  t[max] = 0;
  return t;
}

char* nodec_strdup(const char* s) {
  if (s == NULL) return NULL;
  size_t n = strlen(s);
  return _nodec_strndup(s, n);
}

char* nodec_strndup(const char* s, size_t max) {
  if (s == NULL) return NULL;
  return _nodec_strndup(s, max);
}


/*-----------------------------------------------------------------
  Memory checks
-----------------------------------------------------------------*/
void nodec_check_memory() {
  #if defined(_MSC_VER) && !defined(__clang__)
  # if defined(_DEBUG)
  fprintf(stderr, "\nchecked memory leaks.\n");
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
  _CrtDumpMemoryLeaks();
  # endif
  #endif
}

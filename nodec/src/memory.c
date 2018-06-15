#include "nodec.h"
#include "nodec-internal.h"
#include <uv.h>
#include <assert.h> 

/*-----------------------------------------------------------------
  Wrappers for malloc
-----------------------------------------------------------------*/

void  nodec_freev(lh_value p) {
  nodec_free(lh_ptr_value(p));
}

void* _nodec_malloc(size_t size) {
  return lh_malloc(size);
}

void* _nodec_calloc(size_t count, size_t size) {
  return lh_calloc(count, size);
}

void* _nodec_realloc(void* p, size_t newsize) {
  return lh_realloc(p, newsize);
}

void  _nodec_free(void* p) {
  lh_free(p);
}

char* nodec_strdup(const char* s) {
  return lh_strdup(s);
}

char* nodec_strndup(const char* s, size_t max) {
  return lh_strndup(s, max);
}

void nodec_register_malloc(lh_mallocfun* _malloc, lh_callocfun* _calloc, lh_reallocfun* _realloc, lh_freefun* _free) {
  lh_register_malloc(_malloc, _calloc, _realloc, _free);
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

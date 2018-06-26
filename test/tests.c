#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "tests.h"

static char* output = NULL;
static int total = 0;
static int success = 0;


void tests_check_memory() {
  #if defined(_MSC_VER) && !defined(__clang__)
  # if defined(_DEBUG)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
    _CrtDumpMemoryLeaks();
  # endif
	lh_debug_wait_for_enter();
  #endif
}

void tests_done() {
  printf("\ntests total     : %i\n      successful: %i\n", total, success);
  if (success != total)
    printf("FAILED %i tests\n", total - success);
  else
    printf("all tests were successful.\n");
  lh_print_stats(stderr);
  tests_check_memory();  
}


void test_start(const char* name) {
  printf("----------------\ntesting %s\n----------------\n", name);
}

void test_end(const char* name, const char* expected) {
  total++;
  printf("test %s: ", name);
  if (expected == NULL) {
    success++;
    printf("untested\n");
  }
  else if (output != NULL && strcmp(expected,output)==0) {
    success++;
    printf("SUCCESS\n\n");
  }
  else {
    printf("FAILED!\n");
    printf(" gotten:\n%s\n", output);
    printf(" expected:\n%s\n\n", expected);
  }
  if (output != NULL) {
    free(output);
    output = NULL;
  }
}

void test(const char* name, fun0* f, const char* expected) {
  test_start(name);
  f();
  test_end(name, expected);
}


static void output_cat(const char* s) {
  if (s == NULL) return;
  size_t n = strlen(s);
  if (n == 0) return;
  size_t m = (output==NULL ? 0 : strlen(output)) + n;
  if (output == NULL) {
    output = (char*)malloc(m + 1);
    output[0] = 0;
  }
  else {
    output = (char*)realloc(output, m + 1);
  }
#ifdef HAS_STRNCAT_S  
  strncat_s(output, m + 1, s, n);
#else
  strncat(output,s,n);
#endif
}

void trace_printf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap); fflush(stdout);
  va_end(ap);
}

void test_printf(const char* fmt, ...) {
  char buf[256 + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, 256, fmt, ap);
  buf[256] = 0;
  fputs(buf, stdout); fflush(stdout);
  output_cat(buf);
  va_end(ap);
}



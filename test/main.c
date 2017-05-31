/* ----------------------------------------------------------------------------
  Copyright (c) 2016, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests.h"

/*-----------------------------------------------------------------
  testing
-----------------------------------------------------------------*/
int __cdecl main(void) {
  #if false
  tests_exn();
  tests_state();
  tests_amb();
  tests_dynamic();
  tests_raise();
  tests_general();

  tests_done();
  #else
  test_perf1();
  #endif

  lh_print_stats(stderr);
#if defined(_MSC_VER) && !defined(__clang__)
# if defined(_DEBUG)
  _CrtDumpMemoryLeaks();
# endif
  char buf[128];
  gets_s(buf, 127);
#endif
  return 0;
}

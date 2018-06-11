/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests.h"
#include <errno.h>

/*-----------------------------------------------------------------
testing
-----------------------------------------------------------------*/
static void free_resource(lh_value arg) {
  test_printf("free resource: %i\n", lh_int_value(arg));  
}

static lh_value action1(lh_value arg) {
  int resource = 42;
  LH_DEFER(&free_resource, lh_value_int(resource))
  lh_throw_errno(EDOM);
  LH_DEFER_END
  return 42;
}

static void test_on(lh_actionfun* action) {
  lh_exception* exn;
  lh_value res = lh_try(&exn, action, lh_value_null);
  if (exn != NULL) {
    test_printf("exception: %s\n", exn->msg);
    lh_exception_free(exn);
  }
  else {
    test_printf("success: %li\n", lh_long_value(res));
  }
}

static void run() {
  test_on(action1);
}


void test_exn()
{
  test("builtin exceptions", run,
    "free resource: 42\n"
    "exception: Domain error\n"
  );
}
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
int main(void) {
  printf("testing C: " LH_CCNAME ", " LH_TARGET "\n");

  test_excn();
  test_state();
  test_amb();
  test_dynamic();
  test_raise();
  test_general();
  
  test_tailops();
  test_state_alloc();
  test_yieldn();

  test_exn(); // builtin exceptions

  tests_done();
  return 0;
}

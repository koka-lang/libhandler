/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests++.h"

#include <iostream>

/*-----------------------------------------------------------------
testing
-----------------------------------------------------------------*/
int main(void) {
  std::cout << "testing C++: " << LH_CCNAME << ", " << LH_TARGET << std::endl;
  // {
    test_try();


  // regular c tests compiled as c++  
    test_excn();
    test_state();
    test_amb();

    test_raise();
    test_dynamic();

    test_general();
    test_tailops();
    test_state_alloc();
    test_yieldn();

    // c++ specific tests with destructors, finalizers etc.  test_destructor();
    test_destructor();

    test_exn(); // builtin exceptions

    // Show stats
    tests_done();
  //}
  /*catch (...) {
    fprintf(stderr, "OUCH!\n");
  }*/
  return 0;
}

/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests++.h"

#include <string>
#include <iostream>

class TestDestructor {
private:
  std::string* name;
  int* ip;

public:
  TestDestructor(const std::string& s) {
    name = new std::string(s);
  }
  ~TestDestructor() {
    test_printf("destructor called: %s\n", (this->name!=NULL ? this->name->c_str() : NULL));
    delete name;
    name = NULL;
  }
};

static lh_value test1(lh_value arg) {
  TestDestructor t("test1");
  exn_raise("exn over destructor");
  std::cout << "exiting test1" << std::endl;
  return arg;
}

static void run() {
  lh_value res1 = exn_handle(test1, lh_value_long(42));
  test_printf("test destructor1: %li\n", lh_long_value(res1));
}

void test_destructor() {
  test("destructor", run, 
    "destructor called: test1\n"
    "exception raised: exn over destructor\n"
    "test destructor1: 0\n"
  );
}
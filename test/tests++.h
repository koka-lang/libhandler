#pragma once

#include "tests.h"
#include <string>
#include <iostream>

void test_destructor();
void test_try();

class TestDestructor {
private:
  std::string* name;

public:
  TestDestructor(const std::string& s) {
    name = new std::string(s);
  }
  ~TestDestructor() {
    test_printf("destructor called: %s\n", (this->name != NULL ? this->name->c_str() : NULL));
    delete name;
    name = NULL;
  }
};
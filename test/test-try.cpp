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

LH_DEFINE_EFFECT1(a,foo)
LH_DEFINE_OP0(a,foo,int)

// test raising an exception that is caught over a fragment handler

static lh_value test1(lh_value arg) {
  TestDestructor t("test1");
  int i = a_foo();
  std::cout << "raise in the resume: " << i << std::endl;
  throw "exception from inside resume";
  return arg;
}

/*-----------------------------------------------------------------

-----------------------------------------------------------------*/

static lh_value handle_a_foo(lh_resume r, lh_value local, lh_value arg) {
  // set up a try block here, when resuming, the stack will be overwritten and a fragment installed
  // and the exception should be propagated trought the restored fragment
  try {
    TestDestructor t("testop");
    lh_release_resume(r, local, lh_value_int(42));   
  }
  catch (const char* msg) {
    test_printf("exception caught in operation: %s\n", msg);
    return lh_value_int(42);
  }
  return lh_value_int(0);
}

static const lh_operation _a_ops[] = {
  { LH_OP_GENERAL, LH_OPTAG(a,foo), &handle_a_foo },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef _a_def = { LH_EFFECT(a), NULL, NULL, NULL, _a_ops };

static lh_value a_handle(lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&_a_def, lh_value_null, action, arg);
}

static lh_value a_handle_test1() {
  return a_handle(&test1, lh_value_int(42));
}

/*-----------------------------------------------------------------
  next we test raising an exception from a resume under another
  exception handler than where it was captured
-----------------------------------------------------------------*/

static lh_value handle_a_foo2(lh_resume r, lh_value local, lh_value arg) {
  return lh_value_ptr(r); // return the resumption
}

static const lh_operation _a_ops2[] = {
  { LH_OP_GENERAL, LH_OPTAG(a,foo), &handle_a_foo2 },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef _a_def2 = { LH_EFFECT(a), NULL, NULL, NULL, _a_ops2 };

static lh_value a_handle2(lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&_a_def2, lh_value_null, action, arg);
}

static lh_value a_handle_test2_(lh_resume r) {
  // now resume again and catch the exception
  try {
    return lh_release_resume(r, lh_value_null, lh_value_int(42));
  }
  catch (const char* msg) {
    test_printf("exception caught from resumption: %s\n", msg);
    return lh_value_int(42);
  }
}

static lh_value a_handle_test2() {
  lh_resume r;
  try {
    TestDestructor t("test2");
    r = (lh_resume)lh_ptr_value(a_handle2(test1, lh_value_int(42)));
  }
  catch (...) {
    test_printf("ouch!\n");
    return lh_value_int(0);
  }
  // now resume again and catch the exception, make a call to ensure the stack is different too
  return a_handle_test2_(r);
}

/*-----------------------------------------------------------------
  test releasing a resumption without resuming
-----------------------------------------------------------------*/

static lh_value handle_a_foo3(lh_resume r, lh_value local, lh_value arg) {
  lh_release(r);
  return lh_value_int(42);
}

static const lh_operation _a_ops3[] = {
  { LH_OP_GENERAL, LH_OPTAG(a,foo), &handle_a_foo3 },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef _a_def3 = { LH_EFFECT(a), NULL, NULL, NULL, _a_ops3 };

static lh_value a_handle3(lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&_a_def3, lh_value_null, action, arg);
}

static lh_value a_handle_test3() {
  return a_handle3(&test1, lh_value_int(42));
}

/*-----------------------------------------------------------------

-----------------------------------------------------------------*/

static void run() {
  lh_value res1 = a_handle_test1();
  test_printf("test try1: %li\n", lh_long_value(res1));
  lh_value res2 = a_handle_test2();
  test_printf("test try2: %li\n", lh_long_value(res2));
  lh_value res3 = a_handle_test3();
  test_printf("test try3: %li\n", lh_long_value(res3));
}

void test_try() {
  test("try", run,
    "destructor called: test1\n"
    "destructor called: testop\n"
    "exception caught in operation: exception from inside resume\n"
    "test try1: 42\n"
    "destructor called: test2\n"
    "destructor called: test1\n"
    "exception caught from resumption: exception from inside resume\n"
    "test try2: 42\n"
    "destructor called: test1\n"
    "test try3: 42\n"
  );
}
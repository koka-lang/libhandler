/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests++.h"


static bool realexn = false;

static void raise(const char* s) {
  if (realexn) throw s;
  excn_raise(s);
}



static lh_value test1(lh_value arg) {
  TestDestructor t("test1");
  raise("exn over destructor");
  std::cout << "exiting test1" << std::endl;
  return arg;
}

static lh_value handle_excn_test1() {
  try {
    return excn_handle(test1, lh_value_long(42));
  }
  catch (const char* msg) {
    test_printf("real exn: %s\n", msg);
    return lh_value_long(1);
  }
}
/*-----------------------------------------------------------------

-----------------------------------------------------------------*/

static bool raising() {
  TestDestructor t("test2");
  int i = state_get();
  {
    TestDestructor t2("test2a");
    state_put(i + 1);
    if (i >= 0) {
      raise("raise inside state/amb from 'raising'");
    }
  }
  return true;
}

LH_WRAP_FUN0(raising,bool)

static lh_value handle_amb_raising(lh_value arg) {
  return amb_handle(wrap_raising, arg);
}

static lh_value handle_state_amb_raising(lh_value arg) {
  return state_handle(handle_amb_raising, 0, arg);
}

static blist handle_excn_state_amb_raising() {
  try {
    return lh_blist_value(excn_handle(handle_state_amb_raising, lh_value_null));
  }
  catch (const char* msg) {
    test_printf("test2 real exn: %s\n", msg);
    return blist_nil;
  }
}


static void run() {
  realexn = false;
  lh_value res1 = handle_excn_test1();
  test_printf("test destructor1: %li\n", lh_long_value(res1));
  blist res2 = handle_excn_state_amb_raising();
  blist_print("test destructor2: exn/state/amb raising", res2); printf("\n");

  realexn = true;
  lh_value res1a = handle_excn_test1();
  test_printf("xtest destructor1: %li\n", lh_long_value(res1a));
  blist res2a = handle_excn_state_amb_raising();
  blist_print("xtest destructor2: exn/state/amb raising", res2a); printf("\n");
}

void test_destructor() {
  test("destructor", run, 
    "destructor called: test1\n"
    "exception raised: exn over destructor\n"
    "test destructor1: 0\n"
    "destructor called: test2a\n"
    "destructor called: test2\n"
    "exception raised: raise inside state/amb from 'raising'\n"
    "test destructor2: exn/state/amb raising: []\n"
    "destructor called: test1\n"
    "real exn: exn over destructor\n"
    "xtest destructor1: 1\n"
    "destructor called: test2a\n"
    "destructor called: test2\n"
    "test2 real exn: raise inside state/amb from 'raising'\n"
    "xtest destructor2: exn/state/amb raising: []\n"
  );
}
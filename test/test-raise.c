/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests.h"

/*-----------------------------------------------------------------
   Testing resource free-ing
-----------------------------------------------------------------*/


static bool raising() {
  bool p = amb_flip();
  int i = state_get();
  state_put(i + 1);
  if (i >= 0) {
    excn_raise("raise inside state/amb from 'raising'");
  }
  return p;
}


static lh_value raising_action(lh_value v) {
  unreferenced(v);
  bool p = raising();
  return lh_value_bool(p);
}

/*-----------------------------------------------------------------

-----------------------------------------------------------------*/

static lh_value handle_amb_raising(lh_value arg) {
  return amb_handle(raising_action, arg);
}

static lh_value handle_state_amb_raising(lh_value arg) {
  return state_handle(handle_amb_raising, 0, arg);
}

static blist handle_exn_state_amb_raising() {
  return lh_blist_value(excn_handle(handle_state_amb_raising, lh_value_null));
}

/*-----------------------------------------------------------------
   
-----------------------------------------------------------------*/

static void run() {
// test release of local state
  blist res4 = handle_exn_state_amb_raising();
  blist_print("final result exn/state/amb raising", res4); printf("\n");
}


void test_raise() {
  test("raise resource freeing", run, 
    "exception raised: raise inside state/amb from 'raising'\n"
    "final result exn/state/amb raising: []\n"
  );
}

/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests.h"



/*-----------------------------------------------------------------
  state handler that uses OP_GENERAL
-----------------------------------------------------------------*/

static lh_value _state_result(lh_value local, lh_value x) {
  unreferenced(local);
  //printf("state result: %i, %li\n", *((int*)local), (long)(x));
  return x;
}

static lh_value _state_get(lh_resume sc, lh_value local, lh_value arg) {
  unreferenced(arg);
  unreferenced(sc);
  unreferenced(local);
  trace_printf("state get: %li\n", lh_long_value(local));
  // return lh_value_null;
  //return (*((int*)local) > 0 ? lh_value_null : lh_tail_resume(rcont, lh_value_intptr(*((int*)local))));
  return lh_tail_resume(sc, local, local);
}

static lh_value _state_put(lh_resume sc, lh_value local, lh_value arg) {
  unreferenced(arg);
  trace_printf("state put: %li, %li\n", lh_long_value(local), lh_long_value(arg));
  return lh_tail_resume(sc, arg, lh_value_null);
}

static lh_operation multi_ops[]  = {
  { LH_OP_GENERAL, LH_OPTAG(state,get), &_state_get },
  { LH_OP_GENERAL, LH_OPTAG(state,put), &_state_put },
  { LH_OP_NULL, lh_op_null, NULL }
};
static lh_handlerdef multi_def = { LH_EFFECT(state), NULL, NULL, &_state_result, multi_ops };

lh_value multi_state_handle(lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&multi_def, lh_value_int(0), action, arg);
}



static lh_value handle_state_foo(lh_value arg) {
  return multi_state_handle(wrap_foo, arg);
}

static blist handle_amb_state_foo() {
  return lh_blist_value(amb_handle(handle_state_foo, lh_value_null));
}

static blist handle_state_amb_foo() {
  return lh_blist_value(multi_state_handle(handle_amb_foo, lh_value_null));
}


/*-----------------------------------------------------------------
testing
-----------------------------------------------------------------*/
static void run()
{
  blist res2 = handle_state_amb_foo();
  blist_print("final result multi-state/amb foo", res2); printf("\n");
  blist res3 = handle_amb_state_foo();
  blist_print("final result amb/multi-state foo", res3); printf("\n");
}

void test_general() {
  test("general resume", run, 
    "final result multi-state/amb foo: [false,false,true,true,false]\n"
    "final result amb/multi-state foo: [false,false]\n"
  );
}

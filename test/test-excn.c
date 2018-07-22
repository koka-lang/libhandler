/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests.h"


/*-----------------------------------------------------------------
  Define operations
-----------------------------------------------------------------*/
LH_DEFINE_EFFECT1(excn, raise)
LH_DEFINE_VOIDOP1(excn, raise, lh_string)


/*-----------------------------------------------------------------
  Test programs
-----------------------------------------------------------------*/

lh_value id(lh_value arg) {
  return arg;
}

lh_value id_raise(lh_value arg) {
  excn_raise("an error message from 'id_raise'");
  return arg;
}


/*-----------------------------------------------------------------
  Catch handler
-----------------------------------------------------------------*/

static lh_value _excn_raise(lh_resume sc, lh_value local, lh_value arg) {
  unreferenced(sc);
  unreferenced(local);
  unreferenced(arg);
  test_printf("exception raised: %s\n", lh_lh_string_value(arg));
  return lh_value_null;
}

static const lh_operation _excn_ops[] = {
  { LH_OP_NORESUME, LH_OPTAG(excn,raise), &_excn_raise },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef excn_def = { LH_EFFECT(excn), NULL, NULL, NULL, _excn_ops };

lh_value excn_handle(lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&excn_def, lh_value_null, action, arg);
}

/*-----------------------------------------------------------------
  testing
-----------------------------------------------------------------*/
static void run() {
  lh_value res1 = excn_handle(id, lh_value_long(42));
  test_printf("final result 'id': %li\n", lh_long_value(res1));
  lh_value res2 = excn_handle(id_raise, lh_value_long(42));
  test_printf("final result 'id_raise': %li\n", lh_long_value(res2));
}


void test_excn()
{
  test("exceptions", run,
    "final result 'id': 42\n"
    "exception raised: an error message from 'id_raise'\n"
    "final result 'id_raise': 0\n"
  );
}

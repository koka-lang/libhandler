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
LH_DEFINE_EFFECT1(exn, raise)
LH_DEFINE_VOIDOP1(exn, raise, lh_string)


/*-----------------------------------------------------------------
  Test programs
-----------------------------------------------------------------*/

lh_value id(lh_value arg) {
  return arg;
}

lh_value id_raise(lh_value arg) {
  exn_raise("an error message from 'id_raise'");
  return arg;
}


/*-----------------------------------------------------------------
  Catch handler
-----------------------------------------------------------------*/

static lh_value _exn_raise(lh_resume sc, lh_value local, lh_value arg) {
  unreferenced(sc);
  unreferenced(local);
  unreferenced(arg);
  test_printf("exception raised: %s\n", lh_lh_string_value(arg));
  return lh_value_null;
}

static const lh_operation _exn_ops[] = {
  { LH_OP_NORESUME, LH_OPTAG(exn,raise), &_exn_raise },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef exn_def = { LH_EFFECT(exn), NULL, NULL, NULL, _exn_ops };

lh_value exn_handle(lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&exn_def, lh_value_null, action, arg);
}

/*-----------------------------------------------------------------
  testing
-----------------------------------------------------------------*/
static void run() {
  lh_value res1 = exn_handle(id, lh_value_long(42));
  test_printf("final result 'id': %li\n", lh_long_value(res1));
  lh_value res2 = exn_handle(id_raise, lh_value_long(42));
  test_printf("final result 'id_raise': %li\n", lh_long_value(res2));
}


void tests_exn()
{
  test("exceptions", run,
    "final result 'id': 42\n"
    "exception raised: an error message from 'id_raise'\n"
    "final result 'id_raise': 0\n"
  );
}

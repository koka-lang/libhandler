/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests.h"



/*-----------------------------------------------------------------
  A tail resume handler that raises an exception
-----------------------------------------------------------------*/
LH_DEFINE_EFFECT1(tr, raise)
LH_DEFINE_OP1(tr, raise, long, long)

static lh_value _tr_raise(lh_resume r, lh_value local, lh_value arg) {
  unreferenced(arg);
  test_printf("tail-raise called: %li\n", lh_long_value(arg));
  lh_release(r);  id_raise(arg);
  return lh_tail_resume(r, local, lh_value_long(42));
}

static const lh_operation _tr_ops[] = {
  { LH_OP_TAIL, LH_OPTAG(tr,raise), &_tr_raise },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef tr_def = { LH_EFFECT(tr), NULL, NULL, NULL, _tr_ops };

lh_value tr_handle(lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&tr_def, lh_value_null, action, arg);
}

long tr_test() {
  return tr_raise(42);
}
LH_WRAP_FUN0(tr_test, long)

lh_value tr_handle_test(lh_value arg) {
  return tr_handle(wrap_tr_test, arg);
}

// test[pop-over-skip]
lh_value excn_tr_handle_test(lh_value arg) {
  return excn_handle(tr_handle_test, arg);
}

static void run() {
  lh_value res1 = excn_tr_handle_test(lh_value_long(42));
  test_printf("test res1: %li\n", lh_long_value(res1));
}

void test_tailops() {
  test("tail ops", run,
    "tail-raise called: 42\n"
    "exception raised: an error message from 'id_raise'\n"
    "test res1: 0\n"
  );
}

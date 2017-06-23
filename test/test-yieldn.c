/* ----------------------------------------------------------------------------
Copyright (c) 2016, 2017, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests.h"

LH_DEFINE_EFFECT1(N, sum2)

long N_sum2(long x, long y) {
  return lh_long_value(lh_yieldN(LH_OPTAG(N, sum2), 2, lh_value_long(x), lh_value_long(y)));
}


static lh_value _N_sum2(lh_resume r, lh_value local, lh_value arg) {
  unreferenced(local);
  const yieldargs* ya = lh_yieldargs_value(r,arg);
  long x = lh_long_value(ya->args[0]);
  long y = lh_long_value(ya->args[1]);
  return lh_tail_resume(r, local, lh_value_long(x + y));
}

static lh_operation _N_ops[] = {
  { LH_OP_SCOPED, LH_OPTAG(N,sum2), &_N_sum2 },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef _N_def = { LH_EFFECT(N), NULL, NULL, NULL, _N_ops};

static lh_value N_handle(lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&_N_def, lh_value_null, action, arg);
}

static lh_value test1(lh_value v) {
  long s = N_sum2(lh_long_value(v), 22);
  return lh_value_long(s);
}

static lh_value N_handle_test1() {
  return N_handle(&test1, lh_value_long(20));
}

static void run() {
  lh_value res1 = N_handle_test1();
  test_printf("test sum1: %li\n", lh_long_value(res1));
  _N_ops[0].opkind = LH_OP_TAIL;
  lh_value res2 = N_handle_test1();
  test_printf("test sum2: %li\n", lh_long_value(res2));
}

void test_yieldn() {
  test("yieldn", run,
    "test sum1: 42\n"
    "test sum2: 42\n"
  );
}

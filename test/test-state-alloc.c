/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests.h"

/*-----------------------------------------------------------------
state handler
-----------------------------------------------------------------*/

static lh_value _state_result(lh_value local, lh_value arg) {
  unreferenced(local);
  //trace_printf("state result: %i, %li\n", *((int*)local), (long)(x));
  return arg;
}

static lh_value _state_get(lh_resume rc, lh_value local, lh_value arg) {
  unreferenced(arg);
  //trace_printf("state get: %i\n", *((int*)local));
  return lh_tail_resume(rc, local, lh_value_int( *((int*)lh_ptr_value(local)) ) );
}

static lh_value _state_put(lh_resume rc, lh_value local, lh_value arg) {
  //trace_printf("state put: %i, %li\n", *((int*)local), (long)(arg));
  *((int*)lh_ptr_value(local)) = lh_int_value(arg);
  return lh_tail_resume(rc, local, lh_value_null);
}

static const lh_operation _state_ops[] = {
  { LH_OP_TAIL_NOOP, LH_OPTAG(state,get), &_state_get },
  { LH_OP_TAIL_NOOP, LH_OPTAG(state,put), &_state_put },
  { LH_OP_NULL, lh_op_null, NULL }
};

static lh_value _state_acquire(lh_value local) {
  int* l = (int*)malloc(sizeof(int));
  *l = *((int*)lh_ptr_value(local));
  return lh_value_ptr(l);
}

static void _state_release(lh_value local) {
  free(lh_ptr_value(local));
}

static const lh_handlerdef statex_def = {
  LH_EFFECT(state), &_state_acquire, &_state_release, &_state_result, _state_ops };

lh_value statex_handle(lh_value(*action)(lh_value), int state0, lh_value arg) {
  lh_value local0 = lh_value_ptr(calloc(1, sizeof(int)));
  return lh_handle(&statex_def, local0, action, arg);
}


static lh_value handle_statex_foo(lh_value arg) {
  return statex_handle(wrap_foo, 0, arg);
}

static blist handle_amb_statex_foo() {
  return lh_blist_value(amb_handle(handle_statex_foo, lh_value_null));
}

static blist handle_statex_amb_foo() {
  return lh_blist_value(statex_handle(handle_amb_foo, 0, lh_value_null));
}


/*-----------------------------------------------------------------
testing
-----------------------------------------------------------------*/
static void run() {
  lh_value res1 = statex_handle(state_counter,2,lh_value_null);
  test_printf("final result counterx: %i\n", lh_int_value(res1));
  blist res2 = handle_statex_amb_foo();
  blist_print("final result statex/amb foo", res2); printf("\n");
  blist res3 = handle_amb_statex_foo();
  blist_print("final result amb/statex foo", res3); printf("\n");
}


void test_state_alloc()
{
  test("state allocated", run,
    "final result counterx: 42\n"
    "final result statex/amb foo: [false,false,true,true,false]\n"
    "final result amb/statex foo: [false,false]\n"
  );
}


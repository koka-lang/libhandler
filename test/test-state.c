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
LH_DEFINE_EFFECT2(state, get, put)

LH_DEFINE_OP0(state, get, int)
LH_DEFINE_VOIDOP1(state, put, int)



/*-----------------------------------------------------------------
  Example programs
-----------------------------------------------------------------*/

lh_value state_counter(lh_value arg) {
  unreferenced(arg);
  int i;
  while ((i = state_get()) > 0) {
    trace_printf("counter: %i\n", i);
    state_put(i-1);
  }
  return lh_value_int(42);
}

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
  return lh_tail_resume(rc, local, local);
}

static lh_value _state_put(lh_resume rc, lh_value local, lh_value arg) {
  //trace_printf("state put: %i, %li\n", *((int*)local), (long)(arg));
  return lh_tail_resume(rc, arg, lh_value_null);
}

static const lh_operation _state_ops[] = {
  { LH_OP_TAIL_NOOP, LH_OPTAG(state,get), &_state_get },
  { LH_OP_TAIL_NOOP, LH_OPTAG(state,put), &_state_put },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef state_def = {
  LH_EFFECT(state), NULL, NULL, &_state_result, _state_ops };

lh_value state_handle(lh_value(*action)(lh_value), int state0, lh_value arg) {
  return lh_handle(&state_def, lh_value_int(state0), action, arg);
}


//
//void onread(uv_req req) {
//  resumecont rc = (resumecont)req.data;
//  lh_final_resume(rc, req);
//}
//
//lh_value async_readfilev(resumecont rc, lh_value args) {
//  yieldargs* y = lh_yieldargs_value(args); 
//  uv_req req = malloc(sizeof(uv_req));
//  req.data = rc;
//  real_async_readfile(lh_ptr_value(y->args[0]), lh_int_value(y->args[1]), onread);
//  return lh_value_null;
//}
//
//const char* async_readfile(const char* fname, int mode, int* err) {
//  uv_req* req = lh_ptr_value( lh_yieldN(lh_op_async, lh_value_ptr(fname), lh_value_int(mode)) );
//  *err = req.error;
//  const char* res = req.content;
//  free(req);
//  return res;
//}
//



/*-----------------------------------------------------------------
testing
-----------------------------------------------------------------*/
static void run() {
  lh_value res1 = state_handle(state_counter,2,lh_value_null);
  test_printf("final result counter: %i\n", lh_int_value(res1));
}


void test_state()
{
  test("state", run,
    "final result counter: 42\n"
  );
}


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
LH_DEFINE_EFFECT1(amb, flip)
LH_DEFINE_OP0(amb, flip, bool)


/*-----------------------------------------------------------------
  List of lh_value's
-----------------------------------------------------------------*/

blist blist_nil = NULL;

blist blist_cons(bool b, blist tail) {
  blist res = (blist)malloc(sizeof(struct _bnode));
  res->next = tail;
  res->value = b;
  return res;
}

blist blist_single(bool b) {
  return blist_cons(b, NULL);
}

blist blist_copy(blist xs) {
  if (xs == NULL) return xs;
  return blist_cons(xs->value, blist_copy(xs->next));
}

void blist_appendto(blist xs, blist ys)
{
  while (xs->next != NULL) xs = xs->next;
  xs->next = ys;
}

void blist_free(blist xs) {
  while (xs != NULL) {
    blist next = xs->next;
    free(xs);
    xs = next;
  }
}


static void blist_xprint(const char* msg, blist xs, bool trace ) { 
  (trace ? trace_printf : test_printf)("%s: [", msg);
  blist cur = xs;
  while (cur) {
    (trace ? trace_printf : test_printf)("%s%s", cur->value ? "true" : "false", (cur->next == NULL ? "" : ","));
    cur = cur->next;
  }
  (trace ? trace_printf : test_printf)("]\n");
  blist_free(xs);
}

void blist_print(const char* msg, blist xs) {
  blist_xprint(msg, xs, false);
}

void blist_trace_print(const char* msg, blist xs) {
  blist_xprint(msg, xs, true);
}


/*-----------------------------------------------------------------
Example programs
-----------------------------------------------------------------*/

bool xxor() {
  bool p = amb_flip();
  bool q = amb_flip();
  bool res = ((p || q) && (!(p && q)));
  // trace_printf("xor: %s\n", res ? "true" : "false");
  return res;
}

bool foo() {
  bool p = amb_flip();
  int i = state_get();
  state_put(i + 1);
  if (i > 0 && p) {
    return xxor();
  }
  else {
    return false;
  }
}

LH_WRAP_FUN0(xxor,bool)
LH_WRAP_FUN0(foo,bool)


/*-----------------------------------------------------------------
  ambiguity handler
-----------------------------------------------------------------*/

static lh_value _amb_result(lh_value local, lh_value arg) {
  unreferenced(local);
  bool b = lh_bool_value(arg);
  trace_printf("amb result: %s\n", b ? "true" : "false");
  return lh_value_blist(blist_single(b));
}

static lh_value _amb_flip(lh_resume rc, lh_value local, lh_value arg) {
  unreferenced(arg);
  blist xs = lh_blist_value(lh_call_resume(rc, local, lh_value_bool(false)));
  blist ys = lh_blist_value(lh_release_resume(rc, local, lh_value_bool(true)));
  blist_appendto(xs, ys);
  blist_trace_print("amb flip: result", blist_copy(xs));
  return lh_value_blist(xs);
}

static const lh_operation _amb_ops[] = {
  { LH_OP_GENERAL, LH_OPTAG(amb,flip), &_amb_flip },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef amb_def = { LH_EFFECT(amb), NULL, NULL, &_amb_result, _amb_ops };

lh_value amb_handle(lh_value(*action)(lh_value), lh_value arg) {  
  return lh_handle(&amb_def, 0, action, arg);
}


/*-----------------------------------------------------------------
  convenience wrappers
-----------------------------------------------------------------*/

static blist handle_amb_xor() {
  return lh_blist_value(amb_handle(wrap_xxor, lh_value_null));
}

lh_value handle_amb_foo(lh_value arg) {
  return amb_handle(wrap_foo, arg);
}

static lh_value handle_state_foo(lh_value arg) {
  return state_handle(wrap_foo, 0, arg);
}

static blist handle_amb_state_foo() {
  return lh_blist_value(amb_handle(handle_state_foo, lh_value_null));
}

static blist handle_state_amb_foo() {
  return lh_blist_value(state_handle(handle_amb_foo, 0, lh_value_null));
}


/*-----------------------------------------------------------------
testing
-----------------------------------------------------------------*/
static void run() {
  blist res1 = handle_amb_xor();
  blist_print("final result amb xor", res1); printf("\n");
  blist res2 = handle_state_amb_foo();
  blist_print("final result state/amb foo", res2); printf("\n");
  blist res3 = handle_amb_state_foo();
  blist_print("final result amb/state foo", res3); printf("\n");
}


void test_amb()
{
  test("amb", run, 
    "final result amb xor: [false,true,true,false]\n"
    "final result state/amb foo: [false,false,true,true,false]\n"
    "final result amb/state foo: [false,false]\n"
  );
}


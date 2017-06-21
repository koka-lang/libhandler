/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests.h"

#ifdef HAS__ALLOCA        // msvc runtime
# include <malloc.h>  
# define lh_alloca _alloca
#else 
# include <alloca.h>
# define lh_alloca alloca
#endif

LH_DEFINE_EFFECT1(A,showA)
LH_DEFINE_EFFECT1(B,showB)
LH_DEFINE_OP1(A, showA, long, bool)
LH_DEFINE_OP0(B, showB, long)

/*-----------------------------------------------------------------
  handler for showA; on showA prints A but then returns
  directly with its continuation so it can be resumed later
  perhaps in a different handler context.
-----------------------------------------------------------------*/

static lh_value _showA_result(lh_value local, lh_value arg) {
  unreferenced(local);
  return arg;
}

static lh_value _showA_showA(lh_resume sc, lh_value local, lh_value arg) {
  unreferenced(local);
  bool retcont = lh_value_bool(arg);
  static int count = 0;
  trace_printf("show A: %i\n", count++);
  //showB();               // call B from the operation handler
  trace_printf("exit A: %s\n", (retcont ? "true" : "false") );
  // just return rc immediately (if asked for) and exit the handler
  if (retcont) {
    return lh_value_ptr(sc); // capture: lh_capture(sc));
  }
  else {
    //lh_never_resume(rc);
    lh_release(sc);
    return lh_value_int(42);
  }  
}

static const lh_operation _showA_ops[] = {
  { LH_OP_GENERAL, LH_OPTAG(A,showA), &_showA_showA },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef showA_def = { LH_EFFECT(A), NULL, NULL, &_showA_result, _showA_ops };

static lh_value showA_handle(lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&showA_def, lh_value_null, action, arg);
}

/*-----------------------------------------------------------------
  handler for showB
-----------------------------------------------------------------*/

static lh_value _showB_result(lh_value local, lh_value arg) {
  unreferenced(local);
  return arg;
}

static lh_value _showB_showB(lh_resume sc, lh_value local, lh_value arg) {
  unreferenced(local);
  unreferenced(arg);
  static int count = 0;
  trace_printf("show B: %s %i\n", "test", count++);
  return lh_tail_resume(sc,local,lh_value_null);
}

static const lh_operation _showB_ops[] = {
  { LH_OP_TAIL, LH_OPTAG(B, showB), &_showB_showB },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef showB_def = { LH_EFFECT(B), NULL, NULL, &_showB_result, _showB_ops };

static lh_value showB_handle(lh_value(*action)(lh_value), lh_value arg) {
  return lh_handle(&showB_def, lh_value_null, action, arg);
}

/*-----------------------------------------------------------------
  handler for showB but now shown as BX;
  this is to test dynamic handlers
-----------------------------------------------------------------*/

static lh_value _showBX_result(lh_value local, lh_value arg) {
  unreferenced(local);
  return arg;
}

static lh_value _showBX_showB(lh_resume sc, lh_value local, lh_value arg) {
  unreferenced(local);
  unreferenced(arg);
  static int count = 0;
  trace_printf("show BX: %i\n", count++);
  return lh_tail_resume(sc,local,lh_value_null);
}

static const lh_operation _showBX_ops[] = {
  { LH_OP_TAIL, LH_OPTAG(B, showB), &_showBX_showB },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef showBX_def = { LH_EFFECT(B), NULL, NULL, &_showBX_result, _showBX_ops };

static lh_value showBX_handle(lh_value(*action)(lh_value), lh_value arg) { 
  return lh_handle(&showBX_def, lh_value_null, action, arg);
}



static lh_value _showBY_showB(lh_resume sc, lh_value local, lh_value arg) {
  unreferenced(local);
  unreferenced(arg);
  unreferenced(sc);
  static int count = 0;
  trace_printf("show BY: %i\n", count++);
  // just return
  return lh_value_int(43);
  //return lh_tail_resume(sc, local, lh_value_null);
}

static const lh_operation _showBY_ops[] = {
  { LH_OP_NORESUME, LH_OPTAG(B, showB), &_showBY_showB },
  { LH_OP_NULL, lh_op_null, NULL }
};
static const lh_handlerdef showBY_def = { LH_EFFECT(B), NULL, NULL, NULL, _showBY_ops };

static lh_value showBY_handle(lh_value(*action)(lh_value), lh_value arg) {  
  return lh_handle(&showBY_def, lh_value_null, action, arg);
}

/*-----------------------------------------------------------------
   Test programs
-----------------------------------------------------------------*/

static lh_value test1(lh_value arg) {
  unreferenced(arg);
  B_showB();
  A_showA(true);
  B_showB();
  A_showA(false);
  return lh_value_int(1);
}

static lh_value showA_handle_test1(lh_value arg) {
  return showA_handle(test1, arg);
}

static lh_value showA_handle_test2(lh_value arg) {
  char* p = (char*)lh_alloca(0x1000);
  p[0] = 0;
  return showA_handle(test1, arg);
}

static lh_value test_resume(lh_value rc) {
  trace_printf("resuming..\n");
  return lh_release_resume((lh_resume)lh_ptr_value(rc), lh_value_null, lh_value_null);
}

// test dynamic capture & handling
static lh_value test_dyn1() {
  lh_value rc = showB_handle(showA_handle_test1, lh_value_null); // showA returns a continuation
  trace_printf("returned from showB/showA\n");
  return showBX_handle(test_resume, rc);
}

// test dynamic capture, but now the rc will be under
// the resume on the stack such that no stack needs to be captured by the resume.
static lh_value test_dyn2() {
  lh_value rc = showB_handle(showA_handle_test2, lh_value_null); // showA returns a continuation
  return showBX_handle(test_resume, rc);
}


// test dynamic capture;
// now a is resumed under showB_handle_test1 which allocates first a lot, so it is
// swapped out; however, the resumption calls a B operation so should swap back to here.
static lh_value test_resume1(lh_value rc) {
  char* p = (char*)lh_alloca(0x1000);
  p[0] = 0;
  return lh_release_resume((lh_resume)lh_ptr_value(rc), lh_value_null, lh_value_null);  
}

static lh_value test_dyn3() {
  lh_value rc = showB_handle(showA_handle_test1, lh_value_null); // showA returns a continuation
  return showB_handle(test_resume1, rc);
}



// test dynamic capture & handling;
// the BY just returns so tests a yield_to_handler through a non-scoped frame
// we use test_resume1 so a piece of stack needs to be restored for sure.
static lh_value test_dyn4() {
  lh_value rc = showB_handle(showA_handle_test1, lh_value_null); // showA returns a continuation
  return showBY_handle(test_resume1, rc);
}

static void run() {
  lh_value res1 = test_dyn1();
  test_printf("test dyn1: %i\n", lh_int_value(res1));
  lh_value res2 = test_dyn2();
  test_printf("test dyn2: %i\n", lh_int_value(res2));
  lh_value res3 = test_dyn3();
  test_printf("test dyn3: %i\n", lh_int_value(res3));
  lh_value res4 = test_dyn4();
  test_printf("test dyn4: %i\n", lh_int_value(res4));

}

void test_dynamic() {
  test("dynamic", run, 
    "test dyn1: 42\n"
    "test dyn2: 42\n"
    "test dyn3: 42\n"
    "test dyn4: 43\n"
  );
}

/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

#ifndef __tests_h
#define __tests_h

#include "cenv.h"
#include "libhandler.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/*-----------------------------------------------------------------
  test framework
-----------------------------------------------------------------*/
typedef void fun0();

void tests_check_memory();
void tests_done();
void test(const char* name, fun0* f, const char* expected);
void test_printf(const char* fmt, ...);
void trace_printf(const char* fmt, ...);

/*-----------------------------------------------------------------

-----------------------------------------------------------------*/

#define unreferenced(x) ((void)x)

/* Enable debugging logs on msvc */
#if defined(_MSC_VER) && defined(_DEBUG)
# define _CRTDBG_MAP_ALLOC
# include <crtdbg.h>
#endif


/*-----------------------------------------------------------------
  Exn
-----------------------------------------------------------------*/
LH_DECLARE_EFFECT1(excn, raise)
LH_DECLARE_VOIDOP1(excn, raise, lh_string)

void test_excn();
lh_value id(lh_value);
lh_value id_raise(lh_value);

/*-----------------------------------------------------------------
  State
-----------------------------------------------------------------*/
LH_DECLARE_EFFECT2(state, get, put)
LH_DECLARE_OP0(state, get, int)
LH_DECLARE_VOIDOP1(state, put, int)

lh_value state_counter(lh_value arg);
void test_state();

/*-----------------------------------------------------------------
  Amb
-----------------------------------------------------------------*/
LH_DECLARE_EFFECT1(amb, flip)
LH_DECLARE_OP0(amb, flip, bool)


void test_amb();

bool xxor();
bool foo();

lh_value wrap_xxor(lh_value v);
lh_value wrap_foo(lh_value v);

lh_value handle_amb_foo(lh_value arg);
lh_value amb_handle(lh_value(*action)(lh_value), lh_value arg);
lh_value state_handle(lh_value(*action)(lh_value), int state0, lh_value arg);
lh_value excn_handle(lh_value(*action)(lh_value), lh_value arg);

/*-----------------------------------------------------------------
  Other
-----------------------------------------------------------------*/

void test_dynamic();
void test_raise();
void test_general();
void test_tailops();
void test_state_alloc();
void test_yieldn();
void test_exn();  // builtin exceptions

/*-----------------------------------------------------------------
  List of lh_value's; Declared in tests_amb
-----------------------------------------------------------------*/

struct _bnode {
  struct _bnode* next;
  bool value;
};

typedef struct _bnode* blist;

#define lh_value_blist(l)  lh_value_ptr(l)
#define lh_blist_value(v)  ((blist)(lh_ptr_value(v)))

extern blist blist_nil;
blist blist_cons(bool b, blist tail);
blist blist_single(bool b);
blist blist_copy(blist xs);
void blist_appendto(blist xs, blist ys);
void blist_free(blist xs);
void blist_print(const char* msg, blist xs); // frees too
void blist_trace_print(const char* msg, blist xs); // frees too

#endif

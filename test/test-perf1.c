/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "tests.h"
#include <time.h>
#include <math.h>

#if defined(_MSC_VER) && !defined(__clang__) && !defined(__GNUC__)
# define __noinline     __declspec(noinline)
# define __noopt       
#else
# define __noinline     __attribute__((noinline))
# if defined(__clang__)
#  define __noopt       __attribute__((optnone))
# elif defined(__GNUC__)
#  define __noopt       __attribute__((optimize("O0")))
# else
#  define __noopt       /*no optimization*/
# endif
#endif 

/*-----------------------------------------------------------------

-----------------------------------------------------------------*/


static int __noinline comp(int i) {
  return (int)(sin((float)i));
}

static int counter_native(int i) {
  int sum = 0;
  while (i > 0) {
    sum += comp(i);
    i--;
  }
  return sum;
}

static bool dowork;

static int counter() {
  int i;
  int sum = 0;
  while ((i = state_get()) > 0) {
    sum += (dowork ? comp(i) : 1);
    state_put(i - 1);
  }
  return sum;
}

static lh_value _counter(lh_value arg) {
  unreferenced(arg);
  return lh_value_int(counter());
}

static int counter_eff(int n) {
  return lh_int_value(state_handle(_counter, n, lh_value_null));
}


void test_perf1() {
  int n = 10000000;
  clock_t start = clock();
  int sum1 = counter_native(n);
  clock_t end = clock();
  double t1 = (double)(end - start) / CLOCKS_PER_SEC;

  start = clock();
  int sum2 = counter_eff(n);
  end = clock();
  double t2 = (double)(end - start) / CLOCKS_PER_SEC;

  dowork = true;
  start = clock();
  int sum3 = counter_eff(n);
  end = clock();
  double t3 = (double)(end - start) / CLOCKS_PER_SEC;

  double opsec = (double)(2 * n) / t2;
  printf("native:  %6fs, %i\n", t1, sum1);
  printf("effects: %6fs, %i  (no work)\n", t2, sum2);
  printf("effects: %6fs, %i\n", t3, sum3);
  printf("summary: n=%i, %.3fx slower, %.3fx slower (work), %.3f ops/sec\n", n, t2/t1, t3/t1, opsec);
}


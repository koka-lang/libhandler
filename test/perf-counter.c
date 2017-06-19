/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "perf.h"
#include <math.h>

static const int N = 10000000;

/*-----------------------------------------------------------------

-----------------------------------------------------------------*/

static int __noinline work(int i) {
  return (int)(sqrt((double)i));
}

static int counter_native(int i) {
  int sum = 0;
  while (i > 0) {
    sum += work(i);
    i--;
  }
  return sum;
}

static int counter_nowork() {
  int i;
  int sum = 0;
  while ((i = state_get()) > 0) {
    sum += i;
    state_put(i - 1);
  }
  return sum;
}

static int counter() {
  int i;
  int sum = 0;
  while ((i = state_get()) > 0) {
    sum += work(i);
    state_put(i - 1);
  }
  return sum;
}

static lh_value _counter(lh_value arg) {
  unreferenced(arg);
  return lh_value_int(counter());
}

static lh_value _counter_nowork(lh_value arg) {
  unreferenced(arg);
  return lh_value_int(counter_nowork());
}

static int counter_eff(int n) {
  return lh_int_value(state_handle(_counter, n, lh_value_null));
}
static int counter_eff_nowork(int n) {
  return lh_int_value(state_handle(_counter_nowork, n, lh_value_null));
}


void perf_counter() {
  int n = N;
  
  double t0 = start_clock();
  int sum1 = counter_native(n);
  double t1 = end_clock(t0);

  counter_eff_nowork(n);
  
  t0 = start_clock();
  int sum3 = counter_eff(n);
  double t3 = end_clock(t0);

  t0 = start_clock();
  int sum2 = counter_eff_nowork(n);
  double t2 = end_clock(t0);


  double opsec = (double)(2 * n) / t2;
  printf("native:  %6fs, %i\n", t1, sum1);
  printf("effects: %6fs, %i  (no work)\n", t2, sum2);
  printf("effects: %6fs, %i\n", t3, sum3);
  printf("summary: n=%i, %.3fx slower, %.3fx slower (work)\n", n, t2 / t1, t3 / t1);
  printf("       : %.3fx sqrt, %.3f million ops/sec\n", ((t3 / t1) - 1.0) / 2.0, opsec/1e6);
}


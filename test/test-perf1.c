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
#else
# define __noinline     __attribute__((noinline))
#endif

#ifdef _WIN32
#include <windows.h>
typedef LARGE_INTEGER clockval;
static clockval freq = { { 0, 0 } };

void start_clock(clockval* cv) {
  QueryPerformanceCounter(cv);
}
double end_clock(clockval start) {
  clockval end;
  QueryPerformanceCounter(&end);
  if (freq.QuadPart <= 0) QueryPerformanceFrequency(&freq);
  return (double)(end.QuadPart - start.QuadPart) / (double)(freq.QuadPart);
}
#else
typedef clock_t clockval;

void start_clock(clockval* cv) {
  *cv = clock();
}
double end_clock(clockval start) {
  clockval end = clock();
  return (double)(end - start) / (double)CLOCKS_PER_SEC;
}
#endif 

/*-----------------------------------------------------------------

-----------------------------------------------------------------*/


static int __noinline work(int i) {
  return (int)(sin((float)i));
}

static int counter_native(int i) {
  int sum = 0;
  while (i > 0) {
    sum += work(i);
    i--;
  }
  return sum;
}

static bool dowork = false;

static int counter() {
  int i;
  int sum = 0;
  while ((i = state_get()) > 0) {
    sum += (dowork ? work(i) : 1);
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
  clockval cv;
  
  start_clock(&cv);
  int sum1 = counter_native(n);
  double t1 = end_clock(cv);

  dowork = false;
  start_clock(&cv);
  int sum2 = counter_eff(n);
  double t2 = end_clock(cv);

  dowork = true;
  start_clock(&cv);
  int sum3 = counter_eff(n);
  double t3 = end_clock(cv);

  double opsec = (double)(2 * n) / t2;
  printf("native:  %6fs, %i\n", t1, sum1);
  printf("effects: %6fs, %i  (no work)\n", t2, sum2);
  printf("effects: %6fs, %i\n", t3, sum3);
  printf("summary: n=%i, %.3fx slower, %.3fx slower (work)\n", n, t2 / t1, t3 / t1);
  printf("       : %.3fx sin, %.3f ops/sec\n", ((t3 / t1) - 1.0) / 2.0, opsec);
}


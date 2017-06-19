/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "perf.h"

#ifdef _WIN32
#include <windows.h>
static double freq = 0.0;

static double to_seconds( LARGE_INTEGER t ) {
  if (freq <= 0.0) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    freq = (double)(f.QuadPart);
  }
  return ((double)(t.QuadPart) / freq);
}

static double clock_now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return to_seconds(t);
}
#else
#include <time.h>
#ifdef TIME_UTC
double clock_now() {
  struct timespec t;
  timespec_get(&t,TIME_UTC);
  return (double)t.tv_sec + (1.0e-9 * (double)t.tv_nsec);
}
#else
# warning "no high resolution timer"
double clock_now() {
  return ((double)clock() / (double)CLOCKS_PER_SEC);
}
#endif 
#endif

static double diff = 0.0;

double start_clock() {
  if (diff == 0.0) {
    double t0 = clock_now();
    diff = clock_now() - t0;
  }
  return clock_now();
}

double end_clock(double start) {
  double end = clock_now();
  return (end - start - diff);
}
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
double clock_now() {
  return ((double)clock() / (double)CLOCKS_PER_SEC);
}
#endif 

double start_clock() {
  return clock_now();
}

double end_clock(double start) {
  double end = clock_now();
  return (end - start);
}
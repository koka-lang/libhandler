/* ----------------------------------------------------------------------------
Copyright (c) 2016, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

#ifndef __perf_h
#define __perf_h

#include "tests.h"

#if defined(_MSC_VER) && !defined(__clang__) && !defined(__GNUC__)
# define __noinline     __declspec(noinline)
#else
# define __noinline     __attribute__((noinline))
#endif

struct _clockval;
typedef struct _clockval clockval;

double start_clock();
double end_clock(double start);


/*-----------------------------------------------------------------
  Performance tests
-----------------------------------------------------------------*/
void perf_counter();

#endif

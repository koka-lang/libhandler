/* ----------------------------------------------------------------------------
  Copyright (c) 2016, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "libhandler.h"
#include "perf.h"

/*-----------------------------------------------------------------
  testing
-----------------------------------------------------------------*/
int main(void) 
{
  printf("benchmark: " LH_CCNAME ", " LH_TARGET "\n");
  perf_counter();  

  lh_print_stats(stderr);
  tests_check_memory();
  return 0;
}

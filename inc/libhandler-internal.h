/* ----------------------------------------------------------------------------
Copyright (c) 2016-2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef __libhandler_internal_h 
#define __libhandler_internal_h

/*-----------------------------------------------------------------
  Internal type definitions for libhandler
-----------------------------------------------------------------*/

#include "libhandler.h"

#ifdef __cplusplus
#include <exception>

struct _effecthandler;
typedef struct _effecthandler effecthandler;

// in some cases (like LH_OP_NORESUME) we need to unwind to the effect handler operation
// while calling destructors. We do this using a special unwind exception.
class lh_unwind_exception : public std::exception {
public:
  const effecthandler*  handler;
  lh_opfun*             opfun;
  lh_value              res;

  lh_unwind_exception(const effecthandler* h, lh_opfun* o, lh_value r) : handler(h), opfun(o), res(r) {  }
  lh_unwind_exception(const lh_unwind_exception& e) : handler(e.handler), opfun(e.opfun), res(e.res) {  }

  lh_unwind_exception& operator=(const lh_unwind_exception& e) {
    handler = e.handler;
    opfun = e.opfun;
    res = e.res;
    return *this;
  }

  virtual const char* what() const throw() {
    return "libhandler: unwinding the stack; do not catch this exception!";
  }
};
#endif

#endif
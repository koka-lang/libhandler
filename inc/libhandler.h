/* ----------------------------------------------------------------------------
  Copyright (c) 2016,2017, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef __libhandler_h 
#define __libhandler_h

#define LH_IN_ENCLAVE
  
#include <stdbool.h>  // bool
#include <stdint.h>   // intptr_t
#include <stddef.h>   // ptrdiff_t
#include <stdio.h>    // FILE*

/// \defgroup effect Effect Handlers
/// Library for algebraic effect handlers in C.
/// This is the <a href="https://github.com/koka-lang/libhandler">libhandler</a>
/// project, see that for further information.
/// \{
  

/*-----------------------------------------------------------------
	 Generic values
   Because C has no parametric polymorphism we use `lh_value` as a substitute.
   It is not allowed to pass stack addresses through an `lh_value`!
   All operations can be statically typed though in more expressive type systems.
-----------------------------------------------------------------*/

/// \defgroup effect_value Generic Values 
/// Generic value representations.
/// The #lh_value is used to define generic functions in C where we can convert
/// basic values to and from an #lh_value, using the
/// `lh_<to>_<from>` defines.
/// \{

/// Generic values are represented by an `lh_value`.
/// These can hold a `long long` (which is at least 64-bits).
/// The macros `lh_<to>_<from>` are used to convert to and from `lh_value`s.
typedef long long lh_value;

/// Null value
#define lh_value_null         ((lh_value)0)

/// Convert an #lh_value back to a pointer.
#define lh_ptr_value(v)       ((void*)((intptr_t)(v)))

/// Convert any pointer into a #lh_value.
#define lh_value_any_ptr(p)   ((lh_value)((intptr_t)(p)))


#ifdef NDEBUG
/// Convert a pointer to an #lh_value.
/// The pointer should not point into the stack.
# define lh_value_ptr(p)      lh_value_any_ptr(p)
#else
/// Convert a pointer to an #lh_value.
/// The pointer should not point into the stack.
# define lh_value_ptr(p)      lh_check_value_ptr(p)
#endif

lh_value lh_check_value_ptr(const void* p); // checks if no pointers to the stack are passed in an lh_value

/// Identity; used to aid macros.
#define lh_value_value(v)     (v)

/// Convert an #lh_value back to an `int`.
#define lh_int_value(v)       ((int)v)

/// Convert an `int` to an #lh_value.
#define lh_value_int(i)       ((lh_value)(i))

/// Convert an #lh_value back to a `long`.
#define lh_long_value(v)      ((long)(v))

/// Convert a `long` to an #lh_value.
#define lh_value_long(i)      ((lh_value)(i))

/// Convert an #lh_value back to a `uint64_t`.
#define lh_uint64_t_value(v)      ((uint64_t)(v))

/// Convert a `uint64_t` to an #lh_value.
#define lh_value_uint64_t(i)      ((lh_value)(i))

/// Convert an #lh_value back to a `bool`.
#define lh_bool_value(v)      (lh_int_value(v) != 0 ? (1==1) : (1==0))

/// Convert a `bool` to an #lh_value.
#define lh_value_bool(b)      (lh_value_int(b ? 1 : 0))

/// Type definition for strings to aid with macros.
typedef const char* lh_string;

/// Convert an #lh_value back to a `const char*`.
#define lh_lh_string_value(v) ((const char*)lh_ptr_value(v))

/// Convert a `const char*` to an #lh_value.
#define lh_value_lh_string(v) (lh_value_ptr(v))

#define lh_optag_value(v)     ((lh_optag)lh_ptr_value(v))
#define lh_value_optag(o)     lh_value_ptr(o)


/// Type definition for void pointers to aid with macros.
typedef void* lh_voidptr;

/// Convert an #lh_value back to a `void*`.
#define lh_lh_voidptr_value(v) lh_ptr_value(v)

/// Convert a `void*` to an #lh_value.
#define lh_value_lh_voidptr(p) lh_value_ptr(p)


/// Generic function pointer.
typedef void(lh_voidfun)();

/// \cond
lh_value lh_value_from_fun_ptr(lh_voidfun* fun);
/// \endcond

/// Convert a function pointer into a #lh_value.
#define lh_value_fun_ptr(f)   (lh_value_from_fun_ptr((lh_voidfun*)(f)))

/// Convert an #lh_value back to a function pointer.
lh_voidfun* lh_fun_ptr_value(lh_value v);


/// A generic action working on generic #lh_value s.
typedef lh_value(lh_actionfun)(lh_value);

/// A `lh_resultfun` is called when a handled action is done.
typedef lh_value(lh_resultfun)(lh_value local, lh_value arg);

/// An acquire function copies the local state in a handler when required.
typedef lh_value lh_acquirefun(lh_value local);

/// A release function releases the local state in a handler when required.
typedef void lh_releasefun(lh_value local);


/// \}

/*-----------------------------------------------------------------
	Types
-----------------------------------------------------------------*/

/// \defgroup effect_handlers Handlers
/// These are the primitives to define effect handlers and yield operations
/// \{

/// Continuations are abstract and can only be `resume`d.
struct _lh_resume;

/// A "resume" continuation.
/// This is first-class, and can be stored in data structures etc, and can survive
/// the scope of an operation function. It can be resumed through lh_resume() or lh_release_resume().
typedef struct _lh_resume* lh_resume;

/// Effect values.
/// Operations are identified by a constant string pointer.
/// They are compared by address though so they must be declared as static constants (using #LH_NEWOPTAG)
typedef const char* const * lh_effect;

/// Operation values.
/// An operation is identified by an effect and index in that effect. 
/// There are defined automatically using `LH_DEFINE_OPn` macros and can be referred to 
/// using `LH_OPTAG(effect,opname)`.
typedef const struct lh_optag_ {
  lh_effect effect;
  long      opidx;
} * lh_optag;


/// Operation functions are called when that operation is `yield`ed to. 
typedef lh_value (lh_opfun)(lh_resume r, lh_value local, lh_value arg);


/// Operation kinds. 
/// When defining the operations that a handler can handle, 
/// these are specified to make the handling of operations more efficient. 
/// If you are not sure, #LH_OP_GENERAL is always safe to use :-)
/// At this point #LH_OP_TAIL and #LH_OP_NORESUME are most efficient since they do not need to set up a jump point.
typedef enum _lh_opkind {
  LH_OP_NULL,      ///< Invalid operation (used in static declarations to signal end of the operation array)
  LH_OP_FORWARD,   ///< forwarding operation, the `opfun` should be `NULL` in this case. Never calls this operations but always forwards to the next innermost handler (i.e. a tail-resumptive identity operation).
  LH_OP_NORESUMEX, ///< promise to never resume -- and in C++/SEH, instruct to unwind without even running destructors
  LH_OP_NORESUME,  ///< promise to never resume.
  LH_OP_TAIL_NOOP, ///< promise to not call `yield` and resume at most once, and if resumed, it is the last action performed by the operation function.
  LH_OP_TAIL,      ///< promise to resume at most once, and if resumed, it is the last action performed by the operation function.
  LH_OP_SCOPED,    ///< promise to never resume, or to always resume within the scope of an operation function.
  LH_OP_GENERAL    ///< may resume zero, once, or multiple times, and can be resumed outside the scope of the operation function.
} lh_opkind;

/// Opereation defintion.
/// An `operation` has a kind, an identifying tag, and an associated operation function.
typedef struct _lh_operation {
  lh_opkind  opkind;  ///< Kind of the operation
  lh_optag   optag;   ///< The identifying tag
  lh_opfun*  opfun;   ///< The operation function; use `NULL` (with #LH_OP_FORWARD) to have the operation forwarded to the next enclosing effect (i.e. a direct tail-resume with the same arguments).
} lh_operation;

/// Handler definition.
typedef struct _lh_handlerdef {
  lh_effect           effect;         ///< The Effect being handled.
  lh_acquirefun*      local_acquire;  ///< Called when the local state needs to be acquired. Can be NULL.
  lh_releasefun*      local_release;  ///< Called when the local state is released. Can be NULL.
  lh_resultfun*       resultfun;      ///< Invoked when the handled action is done; can be NULL in which case the action result is passed unchanged.
  const lh_operation* operations;     ///< Definitions of all handled operations ending with an operation with `lh_opkind` `LH_OP_NULL`. Can be NULL to handle no operations;
                                      ///< Note: all operations must be in the same order here as in the effect definition! (since each operation has a fixed index).
} lh_handlerdef;


/*-----------------------------------------------------------------
	 Main interface: `lh_handle`, `lh_yield`, and resume.
   These functions form the core of the algebraic handler abstraction.
-----------------------------------------------------------------*/

/// Handle a particalur effect.
/// Handles operations yielded in `body(arg)` with the given handler definition `def`.
lh_value lh_handle(const lh_handlerdef* def, lh_value local, lh_actionfun* body, lh_value arg);

/// Yield an operation to the nearest enclosing handler. 
lh_value lh_yield(lh_optag optag, lh_value arg);

/// `lh_yield_local` yields to the first enclosing handler for operation `optag` and returns its local state. 
/// This should be used
/// with care as it violates the encapsulation principle but works
/// well for implicit parameters and to reduce the number of explicit
/// operations for many effects.
/// Note, still forwards through effect handlers that use an `lh_opfun` of `NULL`, and, more precisely,
/// returns the state for the innermost enclosing handler that does not have a `NULL` operation.
lh_value lh_yield_local(lh_optag optag);

/*-----------------------------------------------------------------
  Scoped resume
-----------------------------------------------------------------*/

/// Resume a continuation. 
/// Use this when not resuming in a tail position.
lh_value lh_scoped_resume(lh_resume r, lh_value local, lh_value res);

/// Final resumption of a scoped continuation. 
/// Only call `lh_tail_resume` as the last action of an operation function, 
/// i.e. it must occur in tail position of an operation function.
lh_value lh_tail_resume(lh_resume r, lh_value local, lh_value res);


/*-----------------------------------------------------------------
  Resuming first-class continuations
-----------------------------------------------------------------*/
/// Explicitly release a first-class continuation without resuming.
void          lh_release(lh_resume r);

/// Resume a first-class contiunation with a specified result.
lh_value      lh_call_resume(lh_resume r, lh_value local, lh_value res);

/// Resume a first-class contiunation with a specified result. 
/// Also releases the continuation and it cannot be resumed again!
lh_value      lh_release_resume(lh_resume r, lh_value local, lh_value res);


/*-----------------------------------------------------------------
  Convenience functions for yield
-----------------------------------------------------------------*/

/// Adjust a C stack pointer.
/// Inside an operation handler, adjust a pointer that was pointing to the C stack at 
/// capture time to point inside the now captured stack. This can be used to pass
/// values by stack reference to operation handlers. Use with care.
void* lh_cstack_ptr(lh_resume r, void* p);

/// Convert a C stack pointer to an #lh_value.
#define lh_value_cstack_ptr(p)      lh_value_any_ptr(p)

/// Convert an #lh_value back to an adjusted C stack pointer.
/// The pointer is adjusted to now pointing into the heap allocated resumption.
/// This can be used inside operation handlers but use with care.
#define lh_cstack_ptr_value(r,v)    lh_cstack_ptr(r, lh_ptr_value(v))

/// Structure to pass multiple arguments from a yield
typedef struct _yieldargs {
  int      argcount; ///< guaranteed to be >= 0
  lh_value args[1];  ///< allocated to contain `argcount` arguments
} yieldargs;

/// Convert a #yieldargs structure to a generic value
#define lh_value_yieldargs(y)   lh_value_cstack_ptr(y)

/// Convert an #lh_value back to a #yieldargs structure.
#define lh_yieldargs_value(r,v)   ((yieldargs*)lh_cstack_ptr_value(r,v))

/// Yield with multiple arguments. 
/// The operation function gets a `yieldargs*` as its 
/// argument containing `argcount` arguments.  The `yieldarg*` pointer is valid during 
/// the scope of the operation function and freed automatically afterwards.
lh_value lh_yieldN(lh_optag optag, int argcount, ...);

/*-----------------------------------------------------------------
  Operation tags 
-----------------------------------------------------------------*/

/// Null effect.
#define lh_effect_null ((lh_effect)NULL)

/// The _null_ operation tag is used for the final operation struct in a list of operations.
#define lh_op_null  ((lh_optag)NULL)

/// Get the name of an operation tag. 
const char* lh_optag_name(lh_optag optag);

/// Get the name of an effect tag. 
const char* lh_effect_name(lh_effect effect);


/// \}
// handlers

/*-----------------------------------------------------------------
  Debugging
-----------------------------------------------------------------*/

/// \defgroup effect_debug Debugging
/// Helpers for debugging and custom allocators.
/// \{

/// A fatal function is called on fatal errors.
typedef void lh_fatalfun(int err, const char* msg);

/// Type of `malloc` functions.
typedef void* lh_mallocfun(size_t size);
/// Type of `calloc` functions.
typedef void* lh_callocfun(size_t n, size_t size);
/// Type of `realloc` functions.
typedef void* lh_reallocfun(void* p, size_t size);
/// Type of `free` functions.
typedef void  lh_freefun(void* p);

/// Register a function that is called on fatal errors. 
/// Use NULL for the default handler (outputs the error to stderr and exits)
/// - ENOMEM : cannot allocate more memory.
/// - EFAULT : internal error when trying jump into invalid stack frames.
/// - ENOTSUP: trying to generally resume a continuation that where the operation was registered with OP_TAIL or OP_THROW.
/// - ENOSYS : an operation was called but no handler was found for it. 
/// - EINVAL : invalid arguments for an operation.
void lh_register_onfatal(lh_fatalfun* onfatal);


/// Register custom allocation functions
void lh_register_malloc(lh_mallocfun* malloc, lh_callocfun* calloc, lh_reallocfun* realloc, lh_freefun* free);

/// Default `malloc`.
void* lh_malloc(size_t size);
/// Default `calloc`.
void* lh_calloc(size_t count, size_t size);
/// Default `realloc`.
void* lh_realloc(void* p, size_t newsize);
/// Default `free`.
void  lh_free(void* p);
/// Default `strdup`.
char* lh_strdup(const char* s);
/// Default `strndup`.
char* lh_strndup(const char* s, size_t max);

#ifdef LH_IN_ENCLAVE
void lh_print_stats(void* out);
void lh_check_memory(void* out);
#else
/// Print out statistics.
void lh_print_stats(FILE* out);

/// Check at the end of the program if all continuations were released
void lh_check_memory(FILE* out);
#endif

/// Wait for an enter key in debug mode.
void lh_debug_wait_for_enter();

/// \}


/*-----------------------------------------------------------------
  Operation definition helpers
-----------------------------------------------------------------*/

/// \defgroup effect_def Effect Definition helpers
/// Macros to define effect handlers.
/// \{

#define LH_EFFECT(effect)         lh_names_effect_##effect
#define LH_OPTAG_DEF(effect,op)   lh_op_##effect##_##op
#define LH_OPTAG(effect,op)       &LH_OPTAG_DEF(effect,op)

#define LH_DECLARE_EFFECT0(effect)  \
  extern const char* LH_EFFECT(effect)[2];

#define LH_DECLARE_EFFECT1(effect,op1)  \
  extern const char* LH_EFFECT(effect)[3];

#define LH_DECLARE_EFFECT2(effect,op1,op2)  \
  extern const char* LH_EFFECT(effect)[4];

#define LH_DECLARE_OP(effect,op) \
  extern const struct lh_optag_ lh_op_##effect##_##op;

#define LH_DECLARE_OP0(effect,op,restype) \
  LH_DECLARE_OP(effect,op) \
  restype effect##_##op();

#define LH_DECLARE_OP1(effect,op,restype,argtype) \
  LH_DECLARE_OP(effect,op) \
  restype effect##_##op(argtype arg);

#define LH_DECLARE_VOIDOP0(effect,op) \
  LH_DECLARE_OP(effect,op) \
  void effect##_##op();

#define LH_DECLARE_VOIDOP1(effect,op,argtype) \
  LH_DECLARE_OP(effect,op) \
  void effect##_##op(argtype arg);


#define LH_DEFINE_EFFECT0(effect) \
  const char* LH_EFFECT(effect)[2] = { #effect, NULL }; 

#define LH_DEFINE_EFFECT1(effect,op1) \
  const char* LH_EFFECT(effect)[3] = { #effect, #effect "/" #op1, NULL }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op1) = { LH_EFFECT(effect), 0 }; 

#define LH_DEFINE_EFFECT2(effect,op1,op2) \
  const char* LH_EFFECT(effect)[4] = {  #effect, #effect "/" #op1, #effect "/" #op2, NULL }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op1) = { LH_EFFECT(effect), 0 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op2) = { LH_EFFECT(effect), 1 }; 

#define LH_DEFINE_EFFECT3(effect,op1,op2,op3) \
  const char* LH_EFFECT(effect)[5] = {  #effect, #effect "/" #op1, #effect "/" #op2, #effect "/" #op3, NULL }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op1) = { LH_EFFECT(effect), 0 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op2) = { LH_EFFECT(effect), 1 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op3) = { LH_EFFECT(effect), 2 }; 

#define LH_DEFINE_EFFECT4(effect,op1,op2,op3,op4) \
  const char* LH_EFFECT(effect)[6] = {  #effect, #effect "/" #op1, #effect "/" #op2, #effect "/" #op3, #effect "/" #op4, NULL }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op1) = { LH_EFFECT(effect), 0 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op2) = { LH_EFFECT(effect), 1 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op3) = { LH_EFFECT(effect), 2 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op4) = { LH_EFFECT(effect), 3 }; 

#define LH_DEFINE_EFFECT5(effect,op1,op2,op3,op4,op5) \
  const char* LH_EFFECT(effect)[7] = {  #effect, #effect "/" #op1, #effect "/" #op2, #effect "/" #op3, #effect "/" #op4, #effect "/" #op5, NULL }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op1) = { LH_EFFECT(effect), 0 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op2) = { LH_EFFECT(effect), 1 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op3) = { LH_EFFECT(effect), 2 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op4) = { LH_EFFECT(effect), 3 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op5) = { LH_EFFECT(effect), 4 }; 

#define LH_DEFINE_EFFECT6(effect,op1,op2,op3,op4,op5,op6) \
  const char* LH_EFFECT(effect)[8] = {  #effect, #effect "/" #op1, #effect "/" #op2, #effect "/" #op3, #effect "/" #op4, #effect "/" #op5, #effect "/" #op6, NULL }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op1) = { LH_EFFECT(effect), 0 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op2) = { LH_EFFECT(effect), 1 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op3) = { LH_EFFECT(effect), 2 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op4) = { LH_EFFECT(effect), 3 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op5) = { LH_EFFECT(effect), 4 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op6) = { LH_EFFECT(effect), 5 }; 

#define LH_DEFINE_EFFECT7(effect,op1,op2,op3,op4,op5,op6,op7) \
  const char* LH_EFFECT(effect)[9] = {  #effect, #effect "/" #op1, #effect "/" #op2, #effect "/" #op3, #effect "/" #op4, #effect "/" #op5, #effect "/" #op6, #effect "/" #op7, NULL }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op1) = { LH_EFFECT(effect), 0 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op2) = { LH_EFFECT(effect), 1 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op3) = { LH_EFFECT(effect), 2 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op4) = { LH_EFFECT(effect), 3 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op5) = { LH_EFFECT(effect), 4 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op6) = { LH_EFFECT(effect), 5 }; \
  const struct lh_optag_ LH_OPTAG_DEF(effect,op7) = { LH_EFFECT(effect), 6 }; 


#define LH_DEFINE_OP0(effect,op,restype) \
  restype effect##_##op() { lh_value res = lh_yield(LH_OPTAG(effect,op), lh_value_null); return lh_##restype##_value(res); } 

#define LH_DEFINE_OP1(effect,op,restype,argtype) \
  restype effect##_##op(argtype arg) { lh_value res = lh_yield(LH_OPTAG(effect,op), lh_value_##argtype(arg)); return lh_##restype##_value(res); }

#define LH_DEFINE_VOIDOP0(effect,op) \
  void effect##_##op() { lh_yield(LH_OPTAG(effect,op), lh_value_null); } 

#define LH_DEFINE_VOIDOP1(effect,op,argtype) \
  void effect##_##op(argtype arg) { lh_yield(LH_OPTAG(effect,op), lh_value_##argtype(arg)); } 

#define LH_WRAP_FUN0(fun,restype) \
  lh_value wrap_##fun(lh_value arg) { (void)(arg); return lh_value_##restype(fun()); }

#define LH_WRAP_FUN1(fun,argtype,restype) \
  lh_value wrap_##fun(lh_value arg) { return lh_value_##restype(fun(lh_##argtype##_value(arg))); }

#define LH_WRAP_VOIDFUN0(fun) \
  lh_value wrap_##fun(lh_value arg) { (void)(arg); fun(); return lh_value_null; }
  
#define LH_WRAP_VOIDFUN1(fun,argtype) \
  lh_value wrap_##fun(lh_value arg) { fun(lh_##argtype##_value(arg)); return lh_value_null; }

/// \}

/*-----------------------------------------------------------------
  Linear handlers: have either no operations, or only
  operations that do a tail resume without early exit themselves.
-----------------------------------------------------------------*/

/// \defgroup effect_linear Linear Handlers
/// Linear handlers can be scoped inline and are very efficient.
/// These are used to define implicit parameters, defer, and exceptions.
/// \{
void lh_nothing();

#ifdef __cplusplus
class lh_raii_linear_handler {
private:
  ptrdiff_t id;
  void*     hs;    // hstack*
  bool      do_release;
  bool      init;
public:
  lh_raii_linear_handler(const lh_handlerdef* hdef, lh_value local, bool do_release);
  ~lh_raii_linear_handler();
};

#define LH_LINEAR(hdef,local,do_release) \
  lh_raii_linear_handler _lh_linear_handler(hdef,local,do_release); 

#define LH_LINEAR_EXIT(hdef,local,do_release,after) \
  lh_raii_linear_handler _lh_linear_handler(hdef,local,do_release); \
  for (bool _lh_linear_first = true; \
      _lh_linear_first; \
      (after, _lh_linear_first = false))

#else
ptrdiff_t  _lh_linear_handler_init(const lh_handlerdef* hdef, lh_value local, bool* init);
void       _lh_linear_handler_done(ptrdiff_t id, bool init, bool do_release);

#define LH_LINEAR_EXIT(hdef,local,do_release,after)  \
    bool _lh_linear_init = false; \
    ptrdiff_t _lh_linear_id = _lh_linear_handler_init(hdef,local,&_lh_linear_init); \
    for(bool _lh_linear_first = true; \
        _lh_linear_first; \
        (after, _lh_linear_handler_done(_lh_linear_id,_lh_linear_init,do_release), \
          _lh_linear_first=false))

#define LH_LINEAR(hdef,local,do_release)  \
    LH_LINEAR_EXIT(hdef,local,do_release,lh_nothing())

#endif

/*-----------------------------------------------------------------
 Defer: use as
 {defer(free,ptr){ ...  }}
-----------------------------------------------------------------*/
LH_DECLARE_EFFECT0(defer)

#define LH_DEFER_EXIT(after,release_fun,local) \
    const lh_handlerdef _lh_deferdef = { LH_EFFECT(defer), NULL, release_fun, NULL, NULL }; \
    LH_LINEAR_EXIT(&_lh_deferdef,local,true,after)

#define LH_DEFER(release_fun,local) \
    const lh_handlerdef _lh_deferdef = { LH_EFFECT(defer), NULL, release_fun, NULL, NULL }; \
    LH_LINEAR(&_lh_deferdef,local,true)

#define LH_ON_ABORT(release_fun,local)  \
    const lh_handlerdef _lh_deferdef = { LH_EFFECT(defer), NULL, release_fun, NULL, NULL }; \
    LH_LINEAR(&_lh_deferdef,local,false)
/// \} linear

/// \defgroup effect_exn Exceptions and Finally clauses
/// \{

/// Install a _finally_ function that is always run.
/// \param release_fun  A #lh_releasefun that is called on the exit of the scope,
///                     either when returning normally, or when _aborting_, for example
///                     when an exception is thrown.
/// \param local        An #lh_value that is passed to the release function.
///
/// `defer` always needs a scope with double braces. For example:
/// ```
/// void* p = nodec_alloc(10);
/// {defer(nodec_freev,lh_value_ptr(p)){
///   ...
/// }}
/// ```
#define defer_exit(after,release_fun,local) \
    LH_DEFER_EXIT(after,release_fun,local)


/// Install a _finally_ function that is always run.
/// \param release_fun  A #lh_releasefun that is called on the exit of the scope,
///                     either when returning normally, or when _aborting_, for example
///                     when an exception is thrown.
/// \param local        An #lh_value that is passed to the release function.
///
/// `defer` always needs a scope with double braces. For example:
/// ```
/// void* p = nodec_alloc(10);
/// {defer(nodec_freev,lh_value_ptr(p)){
///   ...
/// }}
/// ```
#define defer(release_fun,local) \
    LH_DEFER(release_fun,local)

#define on_abort(release_fun,local) \
    LH_ON_ABORT(release_fun,local)

/// \}   exceptions

/*-----------------------------------------------------------------
  Implicit Parameters:
  {using_implicit(name,value){ ... }}
-----------------------------------------------------------------*/

lh_value _lh_implicit_get(lh_resume r, lh_value local, lh_value arg);

#ifdef __GNUC__
// turn off warnings that (release_fun!=NULL) is always true; TODO: is there a better way to do this?
#pragma GCC diagnostic ignored "-Waddress"
#endif

#define LH_IMPLICIT_EXIT(after,release_fun,local,name) \
    static const lh_operation _lh_imp_ops[2] = { { LH_OP_TAIL_NOOP, LH_OPTAG(name,get), &_lh_implicit_get }, { LH_OP_NULL, lh_op_null, NULL } }; \
    static const lh_handlerdef _lh_imp_hdef  = { LH_EFFECT(name), NULL, release_fun, NULL, _lh_imp_ops }; \
    LH_LINEAR_EXIT(&_lh_imp_hdef,local,(release_fun!=NULL),after)


#define LH_IMPLICIT(release_fun,local,name) \
    LH_IMPLICIT_EXIT(lh_nothing(),release_fun,local,name)

/// \defgroup effect_implicit Implicit Parameters    
/// Functions to easily define and use implicit parameters.
///
/// \b Example
/// ```
/// implicit_define(width);
///
/// void bar() {
///   printf("width is %i\n", lh_int_value(implicit_get(width)));
/// }
///
/// void foo() {
///   {using_implicit(lh_value_int(42),width){
///     bar();
///   }}
/// }
/// ```
/// \{ 

#define using_implicit_defer_exit(after,release_fun,local,name) \
    LH_IMPLICIT_EXIT(after,release_fun,local,name)

#define using_implicit_defer(release_fun,local,name) \
    LH_IMPLICIT(release_fun,local,name)

/// Bind an implicit parameter.
/// \param local The value used for the implicit parameter.
/// \param name The name of the implicit parameter (previously defined using implicit_define())
///
/// \b Example
/// ```
/// implicit_define(width);
///
/// void bar() {
///   printf("width is %i\n", lh_int_value(implicit_get(width)));
/// }
///
/// void foo() {
///   {using_implicit(lh_value_int(42),width){
///     bar();
///   }}
/// }
/// ```
#define using_implicit(local,name) \
    using_implicit_defer(NULL,local,name)

/// Define a new implicit parameter.
/// \param name  The name of the implicit parameter.
#define implicit_define(name) \
    LH_DEFINE_EFFECT1(name,get) 

/// Declare a new implicit parameter.
/// This can be used in header files. There must be a corresponding implicit_define() too.
#define implicit_declare(name) \
    LH_DECLARE_EFFECT1(name,get) LH_DECLARE_OP(name,get) 

/// Get the value of an implicit parameter.
/// \param name The name of a previously defined implicit parameter.
#define implicit_get(name) \
    lh_yield_local(LH_OPTAG(name,get)) 

/// \} implicits

/*-----------------------------------------------------------------
  Standard exceptions
-----------------------------------------------------------------*/

// The Exception effect
LH_DECLARE_EFFECT1(exn, _throw)

/// \addtogroup effect_exn
/// Functions for standard exceptions and finally handlers.
/// \{

/// Standard exception type.
/// Don't use this directly but use the provided functions to allocate and operate
/// on exceptions.
typedef struct _lh_exception {
  int         code;  ///< Exception code, usually an errno_t
  const char* msg;   ///< Optional message.
  void*       data;  ///< Optional user data.
  int         _is_alloced;  ///< 0: static, bits: 0:exception, 1:msg, 2:data, determines if needs free
} lh_exception;

/// Free an exception.
void lh_exception_free(lh_exception* exn);

/// Create an exception.
lh_exception* lh_exception_alloc_ex(int code, const char* msg, void* data, int _is_alloced);
/// Create an exception.
lh_exception* lh_exception_alloc_strdup(int code, const char* msg);
/// Create an exception.
lh_exception* lh_exception_alloc(int code, const char* msg);


/// Throw an exception
void lh_throw(const lh_exception* e);
void lh_throw_errno(int eno);
void lh_throw_str(int code, const char* msg);
void lh_throw_strdup(int code, const char* msg);
void lh_throw_cancel();
lh_exception* lh_exception_alloc_cancel();
bool lh_exception_is_cancel(const lh_exception* exn);

/// Portable way to get a string error message
void lh_strerror( char* buf, size_t len, int eno );

/// Convert an exceptional computation to an exceptional value.
/// If an exception is thrown, `exn` will be set to a non-null value
lh_value lh_try(lh_exception** exn, lh_actionfun* action, lh_value arg);

// Also catch 'uncatchable' exceptions (like cancelation)
lh_value lh_try_all(lh_exception** exn, lh_actionfun* action, lh_value arg);

lh_value lh_finally(lh_actionfun* action, lh_value arg, lh_releasefun* faction, lh_value farg);

/// \} exceptions

/// \}

#endif // __libhandler_h

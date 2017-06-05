/* ----------------------------------------------------------------------------
  Copyright (c) 2016,2017, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* ----------------------------------------------------------------------------
  - We consider parent stack frames to be "below"(or "down") and child frames "above".
    The stack frame of the current function is the "top" of the stack.
    We use this terminology regardless if the stack itself actually grows up or down on 
    the specific architecture.

  - When we capture a stack we always capture a delimited stack up to some handler
    on that stack. When resuming, we always restore the stack *at the exact
    same location*: this is very important when code references addresses on the
    stack, or when on execution platforms that don't allow the stack to reside in
    heap memory. This property is what makes our implementation portable and safe
    (in contrast to many other libraries for general co-routines).
    (It also means we generally need to copy stacks back and forth which may be more
    expensive than direct stack switching)

    The following things may lead to trouble on some platforms:
    - Stacks cannot move during execution. No platform does this by itself (as far as I know)
      but it does mean you cannot move resume functions between threads! A resume
      needs to be executed on the same thread as it was created.
    - First class resumptions (that escape the scope of the handler) can lead
      to a 'fragment' on the C stack. Code cannot unwind such stack beyond the fragment. 
      This could happen with garbage collectors or debuggers for example. Some platforms 
      may need some assembly to properly unwind through fragments. For C++ we need
      to install an exception handler on the fragment boundary to unwind properly.

    Names:
    - The `base` of C stack is always the lowest address; it equals the `top`
      of the stack if the stack grows down, or the `bottom` if the stack grows up.
    - An `entry` is a jump buffer (`_jmp_buf`) and contains the register context; it can 
      be jumped to.
-----------------------------------------------------------------------------*/

#include "libhandler.h"
#include "cenv.h"     // configure generated

#include <stddef.h>   // ptrdiff_t, size_t
#include <stdlib.h>   // exit, malloc
#include <stdio.h>    // fprintf, vfprintf
#include <string.h>   // memcpy
#include <stdarg.h>   // varargs
#include <setjmp.h>   // jmpbuf
#include <assert.h>   // assert
#include <errno.h>    

// maintain cheap statistics
#define _STATS

// Annotate pointer parameters
#define ref
#define out

// define __thread, __noinline, and __noreturn 
#if defined(_MSC_VER) && !defined(__clang__) && !defined(__GNUC__)
# define __thread       __declspec(thread) 
# define __noinline     __declspec(noinline)
# define __noreturn     __declspec(noreturn)
# ifndef __nothrow
#  define __nothrow     __declspec(nothrow)
# endif
# define __returnstwice
# define __noopt       
# define __align(x)     __declspec(align(x))
#else
// assume gcc or clang 
// __thread is already defined
# define __noinline     __attribute__((noinline))
# define __noreturn     __attribute__((noreturn))
# define __returnstwice __attribute__((returns_twice))
# if defined(__clang__)
#  define __noopt       __attribute__((optnone))
# elif defined(__GNUC__)
#  define __noopt       __attribute__((optimize("O0")))
# else
#  define __noopt       /*no optimization*/
# endif
# ifndef __nothrow
#  define __nothrow     __attribute__((nothrow))
# endif
# define __align(x)     __attribute__((align_value(x)))
#endif 


// ENOTSUP is not always defined
#ifndef ENOTSUP
# define ENOTSUP ENOSYS
#endif


/* Select the right definition of setjmp/longjmp;

   We need a _fast_ and _plain_ version where `setjmp` just saves the register 
   context, and `longjmp` just restores the register context and jumps to the saved
   location. Some platforms or libraries try to do more though, like trying
   to unwind the stack on a longjmp to invoke finalizers or saving and restoring signal
   masks. In those cases we try to substitute our own definitions.
*/
#if defined(HAS_ASMSETJMP)
// define the lh_jmp_buf in terms of `void*` elements to have natural alignment
typedef void* lh_jmp_buf[ASM_JMPBUF_SIZE/sizeof(void*)];
__nothrow __returnstwice int  _lh_setjmp(lh_jmp_buf buf);
__nothrow __noreturn     void _lh_longjmp(lh_jmp_buf buf, int arg);

#elif defined(HAS__SETJMP)
# define lh_jmp_buf   jmp_buf
# define _lh_setjmp   _setjmp
# define _lh_longjmp  longjmp

#elif defined(HAS_SIGSETJMP)
 // We use sigsetjmp with a 0 flag to not save the signal mask.
# define lh_jmp_buf    sigjmp_buf
# define _lh_setjmp(x) sigsetjmp(x,0)
# define _lh_longjmp   siglongjmp

#elif defined(HAS_SETJMP)
# define lh_jmp_buf   jmp_buf
# define _lh_setjmp   setjmp
# define _lh_longjmp  longjmp

#else 
# error "setjmp not found!"
#endif

#ifdef HAS__ALLOCA        // msvc runtime
# include <malloc.h>  
# define lh_alloca _alloca
#else 
# include <alloca.h>
# define lh_alloca alloca
#endif

#ifdef HAS_STDBOOL_H
# include <stdbool.h>
#endif
#ifndef __bool_true_false_are_defined
typedef char bool;
# define true  (1==1)
# define false (1==0)
#endif


/*-----------------------------------------------------------------
  Types
-----------------------------------------------------------------*/
// Basic types
typedef unsigned char byte;
typedef ptrdiff_t     count_t;   // signed natural machine word

// forward declarations
struct _handler;

// A handler stack; Seperate from the C-stack so it can be searched even if the C-stack contains fragments
typedef struct _hstack {
  count_t            count;     // number of valid handlers in hframes
  count_t            size;      // total entries available (auto grows exponential upto some limit)
  struct _handler*   hframes;   // array of handlers (0 is bottom frame)
} hstack;

// A captured C stack
typedef struct _cstack {           
  const void*        base;      // The `base` is the lowest adress of where the stack is captured
  count_t            size;      // The byte size of the captured stack
  byte*              frames;    // The captured stack data (allocated in the heap)
} cstack;


// A `fragment` is a captured C-stack and an `entry`.
typedef struct _fragment {
  lh_jmp_buf         entry;     // jump point where the fragment was captured
  cstack             cstack;    // the captured c stack 
  count_t            refcount;  // fragments are allocated on the heap and reference counted.
  volatile lh_value  res;       // when jumped to, a result is passed through `res`
} fragment;

// Operation handlers receive an `lh_resume*`; the kind determines what it points to.
typedef enum _resumekind {
  FullResume,       // `lh_resume` is a `resume`
  TailResume        // `lh_resume` is a `tailresume`
} resumekind;

// Typedeffed to `lh_resume` in the header. 
struct _lh_resume {
  resumekind         rkind;       // the resumption kind
};

// A first-class resumption
typedef struct _resume {
  struct _lh_resume  lhresume;    // contains the kind: always `FullResume` (must be first field, used for casts)
  count_t            refcount;    // resumptions are heap allocated
  lh_jmp_buf         entry;       // jump point where the resume was captured
  cstack             cstack;      // captured cstack
  hstack             hstack;      // captured hstack  always `size == count`
  volatile lh_value  arg;         // the argument to `resume` is passed through `arg`.
} resume;

// A resumption that can only used for tail-call resumptions (`lh_tail_resume`).
typedef struct _tailresume {
  struct _lh_resume  lhresume;    // the kind: always `TailResume` (must be first field, used for casts)
  volatile lh_value  local;       // the new local value for the handler
  volatile bool      resumed;     // set to `true` if `lh_tail_resume` was called
} tailresume;


// Regular effect handler.
typedef struct _effhandler {
  lh_jmp_buf           entry;       // used to jump back to a handler 
  const lh_handlerdef* hdef;        // operation definitions
  volatile lh_value    arg;         // the yield argument is passed here
  const lh_operation*  arg_op;      // the yielded operation is passed here
  resume*              arg_resume;  // the resumption function for the yielded operation
  void*                stackbase;
  lh_value             local;
} effhandler;

// A skip handler.
typedef struct _skiphandler {
  count_t              toskip;      // when looking for an operation handler, skip the next `toskip` frames.
} skiphandler;

// A fragment handler just contains a `fragment`.
typedef struct _fragmenthandler {
  fragment*            fragment;
} fragmenthandler;

// A scoped handler keeps track of the resumption in the scope of
// an operator so it can be released properly.
typedef struct _scopedhandler {
  resume*              resume;
} scopedhandler;


// A handler; there are four kinds of frames
// 1. normal effect handlers, pushed when a handler is called
// 2. a "fragment" handler: these are pushed when a first-class continuation is
//    resumed through `lh_resume` or `lh_release_resume`. Such resume may overwrite parts of the
//    current stack which is saved in its own `fragment` continuation. 
// 3. a "scoped" handler: these are pushed when a `LH_OP_SCOPED` operation is executed
//    to automatically release the resumption function when the scope is exited.
// 4. a "skip" handler: used to skip a number of handler frames for tail call resumptions.
typedef struct _handler {
  lh_effect            effect;    // effect handled
  union {
    effhandler         eff;
    fragmenthandler    frag;      // `effect == LH_EFFECT(__fragment)`    
    scopedhandler      scoped;    // `effect == LH_EFFECT(__scoped)`
    skiphandler        skip;      // `effect == LH_EFFECT(__skip)`  
  } kind;                         // c99 doesn't allow unnamed unions :-(
} handler;

LH_DEFINE_EFFECT0(__skip)
LH_DEFINE_EFFECT0(__fragment)
LH_DEFINE_EFFECT0(__scoped)


// thread local `__hstack` is the 'shadow' handler stack
__thread hstack __hstack = { 0, 0, NULL };


/*-----------------------------------------------------------------
  Fatal errors
-----------------------------------------------------------------*/

static lh_fatalfun* onfatal = NULL;

static void fatal(int err, const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  char buf[256];
  vsnprintf(buf, 255, msg, args);
  va_end(args);
  if (onfatal != NULL) {
    onfatal(err,buf);
  }
  else {
    fflush(stdout);
    fputs("libhandler: fatal error: ", stderr);
    fputs(buf, stderr);
    fputs("\n", stderr);
    exit(1);
  }
}

// Set up a different handler for fatal errors
void lh_register_onfatal(lh_fatalfun* _onfatal) {
  onfatal = _onfatal;
}

// Set up different allocation functions
static lh_mallocfun* custom_malloc = NULL;
static lh_reallocfun* custom_realloc = NULL;
static lh_freefun* custom_free = NULL;

void lh_register_malloc(lh_mallocfun* _malloc, lh_reallocfun* _realloc, lh_freefun* _free) {
  custom_malloc = _malloc;
  custom_realloc = _realloc;
  custom_free = _free;
}

// Allocate memory and call `fatal` when out-of-memory
#if defined(_MSC_VER) && defined(_DEBUG)
  // Enable debugging logs on msvc 
# undef _malloca // suppress warning
# define _CRTDBG_MAP_ALLOC
# include <crtdbg.h>
# define checked_malloc malloc
# define checked_realloc realloc
# define checked_free free
#else
static void* checked_malloc(size_t size) {
  //assert((ptrdiff_t)(size) > 0); // check for overflow or negative sizes
  if ((ptrdiff_t)(size) <= 0) fatal(EINVAL, "invalid memory allocation size: %lu", (unsigned long)size );
  void* p = (custom_malloc==NULL ? malloc(size) : custom_malloc(size));
  if (p == NULL) fatal(ENOMEM, "out of memory");
  return p;
}
static void* checked_realloc(void* p, size_t size) {
  //assert((ptrdiff_t)(size) > 0); // check for overflow or negative sizes
  if ((ptrdiff_t)(size) <= 0) fatal(EINVAL, "invalid memory re-allocation size: %lu", (unsigned long)size);
  void* q = (custom_realloc==NULL ? realloc(p,size) : custom_realloc(p,size));
  if (q == NULL) fatal(ENOMEM, "out of memory");
  return q;
}
static void checked_free(void* p) {
  if (p == NULL) return;
  if (custom_free == NULL) free(p);
  else custom_free(p);
}
#endif



/*-----------------------------------------------------------------
  Stack helpers; these abstract over the direction the C stack grows.
  The functions here give an interface _as if_ the stack
  always grows 'up' with the 'top' of the stack at the highest absolute address.
-----------------------------------------------------------------*/

// approximate the top of the stack
static __noinline __noopt void* get_cstack_top() {
  auto byte* top = (byte*)&top;
  return top;
}

// true if the stack grows up
static bool stackup = false;

// base of our c stack
static const void* stackbottom = NULL;

// infer the direction in which the stack grows and the size of a stack frame 
static __noinline __noopt void infer_stackdir() {
  auto void* mark = (void*)&mark;
  void* top = get_cstack_top();
  stackup = (mark < top);
  stackbottom = mark;
}

// The difference between stack pointers (pretending the stack grows up)
// i.e. it is `p - q` for a stack that grows up, but `q - p` for a stack that grows down.
static ptrdiff_t stack_diff(const void* p, const void* q) {
  ptrdiff_t diff = (byte*)p - (byte*)q;
  return (stackup ? diff : -diff);
}

// The address of the bottom of the stack given the `base` and `size` of a stack.
static const void* stack_bottom(const void* base, ptrdiff_t size) {
  return (stackup ? base : (byte*)base + size);
}

// The address of the top of the stack given the `base` and `size` of the stack.
static const void* stack_top(const void* base, ptrdiff_t size) {
  return (stackup ? (byte*)base + size : base);
}

// Is an address `below` another in the stack?
// i.e. if the stack grows up `p < q` and otherwise `p > q`
static bool stack_isbelow(const void* p, const void* q) {
  return (stackup ? p < q : p > q);
}

#ifndef NDEBUG
static bool in_cstack(const void* p) {
  const void* top = get_cstack_top();
  return !(stack_isbelow(top, p) || stack_isbelow(p, stackbottom));
}

lh_value lh_value_ptr(const void* p) {
  if (in_cstack(p)) fatal(EINVAL,"Cannot pass pointers to the c-stack in a lh_value");
  return ((lh_value)(p));
}
#endif

/*-----------------------------------------------------------------
  Predefined operators
-----------------------------------------------------------------*/

const char* lh_effect_name(lh_effect effect) {
  return (effect== NULL ? "<null>" : effect[0]);
}

const char* lh_optag_name(lh_optag optag) {
  return (optag == NULL ? "<null>" : optag->effect[optag->opidx+1]);
}

/*-----------------------------------------------------------------
   Maintain statistics
-----------------------------------------------------------------*/
#ifdef _DEBUG
// maintain detailed statistics
# define _DEBUG_STATS
#endif


static struct {
  long rcont_captured_scoped;
  long rcont_captured_resume;
  long rcont_captured_fragment;
  long rcont_captured_empty;
  count_t rcont_captured_size;

  long rcont_resumed_scoped;
  long rcont_resumed_resume;
  long rcont_resumed_fragment;
  long rcont_resumed_tail;
  
  long rcont_released;
  count_t rcont_released_size;

  long operations;
} stats = {
    0, 0, 0, 0, 0,
    0, 0, 0, 
    0, 0,
    0, 
};

void lh_print_stats(FILE* h) {
  static const char* line = "--------------------------------------------------------------\n";
  #ifdef _STATS
  if (h == NULL) h = stderr;
  fputs(line, h);
  long captured = stats.rcont_captured_scoped + stats.rcont_captured_resume + stats.rcont_captured_fragment;
  long resumed = stats.rcont_resumed_scoped + stats.rcont_resumed_resume + stats.rcont_resumed_fragment + stats.rcont_resumed_tail;
  if (captured != stats.rcont_released) {
    fputs("libhandler: memory leaked: not all continuations are released!\n", h);
  }
  else {
    fputs("libhandler statistics:\n", h);
  }
  if (captured > 0) {
    fputs("resume cont:\n", h);
    fprintf(h, "  resumed     :%li\n", resumed);
    fprintf(h, "    resume    :%6li\n", stats.rcont_resumed_resume);
    fprintf(h, "    scoped    :%6li\n", stats.rcont_resumed_scoped);
    fprintf(h, "    fragment  :%6li\n", stats.rcont_resumed_fragment);
    #ifdef _DEBUG_STATS
    fprintf(h, "    tail      :%6li\n", stats.rcont_resumed_tail);
    #endif
    fprintf(h, "  captured    :%li\n", captured);
    fprintf(h, "    resume    :%6li\n", stats.rcont_captured_resume);
    fprintf(h, "    scoped    :%6li\n", stats.rcont_captured_scoped);
    fprintf(h, "    fragment  :%6li\n", stats.rcont_captured_fragment);
    fprintf(h, "    empty     :%6li\n", stats.rcont_captured_empty);
    fprintf(h, "    total size:%6li kb\n", (long)((stats.rcont_captured_size + 1023) / 1024));
    fprintf(h, "    avg size  :%6li bytes\n", (long)((stats.rcont_captured_size / (captured > 0 ? captured : 1))));
    if (captured != stats.rcont_released) {
      fprintf(h, "  released    :%li\n", stats.rcont_released);
      fprintf(h, "    total size:%6li kb\n", (long)((stats.rcont_released_size + 1023) / 1024));
    }
  }
  # ifdef _DEBUG_STATS
  fputs("operations:\n", h);
  fprintf(h, "  total       :%6li\n", stats.operations);
  # endif
  fputs(line, h);
  #endif
}

// Check if all continuations were released. If not, print out statistics.
void lh_check_memory(FILE* h) {
  #ifdef _STATS
  count_t captured = stats.rcont_captured_scoped + stats.rcont_captured_resume + stats.rcont_captured_fragment; 
  if (captured != stats.rcont_released) {
    lh_print_stats(h);
  }
  #endif
}

/*-----------------------------------------------------------------
  Cstack
-----------------------------------------------------------------*/
static void cstack_init(ref cstack* cs) {
  assert(cs != NULL);
  cs->base = NULL;
  cs->size = 0;
  cs->frames = NULL;
}

static void cstack_free(ref cstack* cs) {
  assert(cs != NULL);
  if (cs->frames != NULL) {
    checked_free(cs->frames);
    cs->frames = NULL;
    cs->size = 0;
  }
}


// Return the lowest address to a c-stack regardless if the stack grows up or down
static const void* cstack_base(const cstack* cs) {
  return cs->base;
}

// Return the top of the c-stack
static const void* cstack_top(const cstack* cs) {
  return stack_top(cs->base, cs->size);
}

// Return the bottom of the c-stack
static const void* cstack_bottom(const cstack* cs) {
  return stack_bottom(cs->base, cs->size);
}



/*-----------------------------------------------------------------
  Fragments
-----------------------------------------------------------------*/

static void hstack_free(ref hstack* hs);

// release a continuation; returns `true` if it was released
static __noinline void fragment_free_(fragment* f) {
  #ifdef _STATS
  stats.rcont_released++;
  stats.rcont_released_size += (long)f->cstack.size;
  #endif
  cstack_free(&f->cstack);
  f->refcount = -1; // for debugging
  checked_free(f);
}

static void _fragment_release(fragment* f) {
  assert(f->refcount > 0);
  f->refcount--;
  if (f->refcount == 0) fragment_free_(f);
}

// Release a fragment
static void fragment_release(fragment* f) {
  if (f!=NULL) _fragment_release(f);
}

// Release a resume continuation and set to `NULL`
static void fragment_release_at(fragment* volatile * pf) {
  fragment_release(*pf);
  *pf = NULL;
}

// Copy a reference to a resume continuation, increasing its reference count.
static fragment* fragment_acquire(fragment* f) {
  assert(f != NULL);
  if (f != NULL) {
    assert(f->refcount > 0);
    f->refcount++;
  }
  return f;
}


/*-----------------------------------------------------------------
  Resumptions
-----------------------------------------------------------------*/

// release a resumptions; returns `true` if it was released
static __noinline void resume_free_(resume* r) {
  #ifdef _STATS
  stats.rcont_released++;
  stats.rcont_released_size += (long)r->cstack.size + (long)r->hstack.size * sizeof(handler);
  #endif
  cstack_free(&r->cstack);
  hstack_free(&r->hstack);
  r->refcount = -1; // for debugging
  checked_free(r);
}

static void _resume_release(resume* r) {
  assert(r->lhresume.rkind == FullResume);
  assert(r->refcount > 0);
  r->refcount--;
  if (r->refcount == 0) resume_free_(r);
}

// Release a resumption
static void resume_release(resume* r) {
  if (r != NULL) _resume_release(r);
}

// Release a resumption and set it to NULL;
static void resume_release_at(ref resume** pr) {
  resume_release(*pr);
  *pr = NULL;
}

// Acquire a resumption by increasing its reference count.
static resume* resume_acquire(resume* r) {
  assert(r!=NULL);
  if (r != NULL) {
    assert(r->lhresume.rkind==FullResume);
    assert(r->refcount > 0);
    r->refcount++;
  }
  return r;
}


/*-----------------------------------------------------------------
  Handler
-----------------------------------------------------------------*/

static bool is_skiphandler(const handler* h) {
  return (h->effect == LH_EFFECT(__skip));
}

static bool is_fragmenthandler(const handler* h) {
  return (h->effect == LH_EFFECT(__fragment));
}

static bool is_scopedhandler(const handler* h) {
  return (h->effect == LH_EFFECT(__scoped));
}

#ifndef NDEBUG
static bool is_effhandler(const handler* h) {
  return (!is_skiphandler(h) && !is_fragmenthandler(h) && !is_scopedhandler(h));
}
#endif

// Increase the reference count of a handler's `rcont`.
static handler* handler_acquire(handler* h) {
  if (is_fragmenthandler(h)) {
    fragment_acquire(h->kind.frag.fragment);
  }
  else if (is_scopedhandler(h)) {
    resume_acquire(h->kind.scoped.resume);
  }
  return h;
}

// Decrease the reference count of a handler's `rcont`.
static void handler_release(ref handler* h) {
  if (is_fragmenthandler(h)) {
    fragment_release_at(&h->kind.frag.fragment);
  }
  else if (is_scopedhandler(h)) {
    resume_release_at(&h->kind.scoped.resume);
  }
}

/*-----------------------------------------------------------------
  Handler stacks
-----------------------------------------------------------------*/

// Handler stacks increase exponentially in size up to a limit, then increase linearly
#define HMINSIZE     32
#define HMAXEXPAND 2048

static count_t hstack_goodsize(count_t needed) {
  if (needed > HMAXEXPAND) {
    return (HMAXEXPAND * ((needed + HMAXEXPAND - 1) / HMAXEXPAND)); // round up to next HMAXEXPAND
  }
  else {
    count_t newsize;
    for (newsize = HMINSIZE; newsize < needed; newsize *= 2) {}  // round up to next power of 2
    return newsize;
  }
}

static void hstack_realloc_(ref hstack* hs, count_t needed) {
  count_t newsize = hstack_goodsize(needed);
  hs->hframes = checked_realloc(hs->hframes, sizeof(handler)*newsize);
  hs->size = newsize;
}

// Ensure the handler stack is big enough for `extracount` handlers.
static void hstack_ensure_space(ref hstack* hs, count_t extracount) {
  count_t needed = hs->count + extracount;
  if (needed > hs->size) hstack_realloc_(hs, needed);
}

// Initialize a handler stack
static void hstack_init(ref hstack* hs) {
  hs->count = 0;
  hs->size = 0;
  hs->hframes = NULL;
}

// Current top handler frame
static handler* hstack_top(ref hstack* hs) {
  assert(hs->count > 0);
  return &hs->hframes[hs->count - 1];
}

// Push a new uninitialized handler frame and return a reference to it.
static handler* _hstack_push(ref hstack* hs, const lh_effect effect) {
  hstack_ensure_space(hs, 1);
  hs->count++;
  handler* h = &hs->hframes[hs->count - 1];
  h->effect = effect;
  return h;
}

// Push an effect handler
static handler* hstack_push_effect(ref hstack* hs, const lh_handlerdef* hdef, void* stackbase, lh_value local) {
  handler* h = _hstack_push(hs, hdef->effect);
  h->kind.eff.stackbase = stackbase;
  h->kind.eff.hdef = hdef;
  h->kind.eff.local = local;
  h->kind.eff.arg = lh_value_null;
  h->kind.eff.arg_op = NULL;
  h->kind.eff.arg_resume = NULL;
  return h;
}

// Push a skip handler
static handler* hstack_push_skip(ref hstack* hs, count_t toskip) {
  handler* h = _hstack_push(hs, LH_EFFECT(__skip));
  h->kind.skip.toskip = toskip;
  return h;
}

// Push a fragment handler
static handler* hstack_push_fragment(ref hstack* hs, fragment* fragment) {
  handler* h = _hstack_push(hs, LH_EFFECT(__fragment));
  h->kind.frag.fragment = fragment;
  return h;
}

// Push a scoped handler
static handler* hstack_push_scoped(ref hstack* hs, resume* resume) {
  handler* h = _hstack_push(hs, LH_EFFECT(__scoped));
  h->kind.scoped.resume = resume;
  return h;
}

// Pop a handler frame, decreasing its reference counts.
static void hstack_pop(ref hstack* hs) {
  assert(hs->count > 0);
  handler_release(&hs->hframes[hs->count - 1]);
  hs->count--;
}

// Pop a skip frame
static void hstack_pop_skip(ref hstack* hs) {
  assert(hs->count > 0 && is_skiphandler(&hs->hframes[hs->count-1]));
  hs->count--;
}

// Release the handler frames of an `hstack`
static void hstack_free(ref hstack* hs) {
  assert(hs!=NULL);
  if (hs->hframes != NULL) {
    while (hs->count > 0) {
      hstack_pop(hs); // decrease reference counts (but don't restore fragments)
    }
    checked_free(hs->hframes);
    hs->size = 0;
    hs->hframes = NULL;
  }
}


static count_t _hstack_append_movefrom(ref hstack* hs, ref hstack* topush, count_t idx) {
  count_t cnt = topush->count - idx;
  hstack_ensure_space(hs, cnt);
  memcpy(hs->hframes + hs->count, topush->hframes + idx, sizeof(handler) * cnt);
  hs->count += cnt;
  return cnt;
}

// Copy handlers from one stack to another increasing reference counts as appropiate.
static void hstack_append_copyfrom(ref hstack* hs, ref hstack* tocopy, count_t idx) {
  count_t copied = _hstack_append_movefrom(hs, tocopy, idx);
  for (count_t i = 0; i < copied; i++) {
    handler_acquire(&hs->hframes[hs->count - i - 1]);    
  }
}

// Find an operation that handles `optag` in the handler stack.
static handler* hstack_find(ref hstack* hs, lh_optag optag, out const lh_operation** op, out lh_value* local, out count_t* skipped) {
  handler* hframes = hs->hframes;
  count_t i = hs->count;
  while (i > 0) {
    i--;
    handler* h = &hframes[i];
    if (h->effect == optag->effect) {
      assert(h->kind.eff.hdef != NULL);
      const lh_operation* oper = &h->kind.eff.hdef->operations[optag->opidx];
      assert(oper->optag == optag); // can fail if operations are defined in a different order than declared
      if (oper->opfun != NULL) {
        *skipped = hs->count - i - 1;
        *op = oper;
        *local = h->kind.eff.local;
        return h;
      }
    }
    else if (is_skiphandler(h)) {
      i -= h->kind.skip.toskip; // skip `topskip` handler frames if needed
    }
  }
  fatal(ENOSYS,"no handler for operation found: '%s'", lh_optag_name(optag));
  *skipped = 0;
  *op = NULL;
  *local = lh_value_null;
  return NULL;
}

static count_t hstack_indexof(const hstack* hs, const handler* h) {
  assert(hs->count > 0 && h >= hs->hframes && h <= &hs->hframes[hs->count - 1]);
  return (h - hs->hframes);
}

static handler* hstack_at(const hstack* hs, count_t idx) {
  assert(hs->count > idx);
  return &hs->hframes[idx];
}

/*-----------------------------------------------------------------
  Unwind a handler stack
  This is a bit involved since it requires popping one frame at a
  time decreasing reference counts, but also when a "fragment" handler
  is encountered we need to restore its saved stack. We cannot do that
  right away though as that may overwrite our own stack frame. Therefore,
  the unwinding returns a `cstack` object that should be restored when
  possible. 

  Todo: optimize this more to avoid an initial copy if the refcount
  on ds would drop to zero anyways; also, we could first scan to which
  base address we are going to jump and only restores pieces of stack
  below that.
-----------------------------------------------------------------*/
const byte* _min(const byte* p, const byte* q) { return (p <= q ? p : q); }
const byte* _max(const byte* p, const byte* q) { return (p >= q ? p : q); }

// Extend cstack `cs` in-place to encompass both the `ds` stack and itself.
static void cstack_extendfrom(ref cstack* cs, const ref cstack* ds) {
  const byte* csp = cstack_base(cs);
  const byte* dsp = cstack_base(ds);
  if (cs->frames == NULL) {
    // nothing yet, just copy `ds`
    if (ds->frames != NULL) {
      cs->frames = checked_malloc(ds->size);
      memcpy(cs->frames, ds->frames, ds->size);
      cs->base = ds->base;
      cs->size = ds->size;
    }
  }
  else {
    // otherwise extend such that we can merge `cs` and `ds` together
    const byte* newsp = _min(csp,dsp);
    ptrdiff_t newsize = _max(csp + cs->size, dsp + ds->size) - newsp;
    if (newsize > cs->size) {
      cs->frames = checked_realloc(cs->frames, newsize);
      if (newsp != csp) {
        assert(csp > newsp);
        memmove(cs->frames + (csp - newsp), cs->frames, cs->size);
        cs->base = csp;
      }
      cs->size = newsize;
    }
    assert(dsp >= newsp);
    memcpy(cs->frames + (dsp - newsp), ds->frames, ds->size);
  }
}


// Pop the stack up to the given handler `h` (which should reside in `hs`)
// Return a stack object in `cs` (if not `NULL) that should be restored later on.
static void hstack_pop_upto(ref hstack* hs, ref handler* h, out cstack* cs) 
{
  count_t hidx = hstack_indexof(hs, h);
  assert(hs->count > hidx);
  assert(&hs->hframes[hidx] == h);
  if (cs != NULL) cstack_init(cs);
  count_t cnt = hs->count - hidx - 1;
  for (count_t i = 0; i < cnt; i++) {
    handler* hf = &hs->hframes[hs->count - 1];
    if (is_fragmenthandler(hf)) { // (hf->hdef == NULL && hf->rcont != NULL && cs != NULL) {
      // special "fragment" handler; remember to restore the stack
      assert(hf->kind.frag.fragment != NULL);
      cstack* hcs = &hf->kind.frag.fragment->cstack;
      if (hcs != NULL) {
        cstack_extendfrom(cs, hcs);
      }
    }
    hstack_pop(hs);
  }
  assert(hs->count == hidx + 1);
  assert(hstack_top(hs) == h);
}


/*-----------------------------------------------------------------
  Initialize globals
-----------------------------------------------------------------*/

static bool initialized = false;

static __noinline bool _lh_init(hstack* hs) {
  if (!initialized) {
    initialized = true;
    infer_stackdir();
    #ifdef ASM_JMPBUF_SIZE
    if (sizeof(lh_jmp_buf) < ASM_JMPBUF_SIZE) {
      fatal(EFAULT, "runtime jmp_buf is smaller than required by the libhander routines!");
    }
    #endif
  }
  assert(__hstack.size==0 && hs == &__hstack);
  hstack_init(hs);
  return true;
}

static bool lh_init(hstack* hs) {
  if (hs->size!=0) return false;
              else return _lh_init(hs);
}

static __noinline void lh_done(hstack* hs) {
  assert(hs == &__hstack && hs->size!=0 && hs->count==0 );
  hstack_free(hs);
}


/*-----------------------------------------------------------------
  Internal: Jump to a context
-----------------------------------------------------------------*/

// `_jumpto_stack` jumps to a given entry with a given c-stack to restore.
// It is called from `jumpto` which ensures through an `alloca` that it will
// run in a stack frame just above the stack we are restoring (so the local 
// variables will remain in-tact. The `no_opt` parameter is there so 
// smart compilers (i.e. clang) will not optimize away the `alloca` in `jumpto`.
static __noinline __noreturn __noopt void _jumpto_stack(
  byte* cframes, ptrdiff_t size, byte* base, lh_jmp_buf* entry, bool freecframes,
  char* no_opt)
{
  if (no_opt != NULL) no_opt[0] = 0;
  // copy the saved stack onto our stack
  memcpy(base, cframes, size);        // this will not overwrite our stack frame 
  if (freecframes) { free(cframes); } // should be fine to call `free` (assuming it will not mess with the stack above its frame)
  // and jump 
  _lh_longjmp( *entry, 1);
}

/* jump to `entry` while restoring cstack `cs` and pushing handlers `hs` onto the global handler stack.
   Set `freecframes` to `true` to release the cstack after jumping.
*/
static __noinline __noreturn void jumpto(
  cstack* cs, hstack* hs, lh_jmp_buf* entry, bool freecframes )
{
  // push on handler chain 
  if (hs != NULL) {
    hstack_append_copyfrom(&__hstack, hs, 0);
  }
  if (cs->frames == NULL) {
    // if no stack, just jump back down the stack; 
    // sanity: check if the entry is really below us!
    void* top = get_cstack_top();
    if (cs->base != NULL && stack_isbelow(top,cstack_top(cs))) {
      fatal(EFAULT,"Trying to jump up the stack to a scope that was already exited!");
    }
    // long jump back up direcly, no need to restore stacks
    _lh_longjmp(*entry, 1);
  }
  else {
    // ensure there is enough room on the stack; 
    void* top = get_cstack_top();
    ptrdiff_t extra = stack_diff(cstack_top(cs), top);
                      // (stackdown ? ptrdiff(top, cs->bottom) : ptrdiff(cs->bottom,top)) + cs->size;
    extra += 0x200; // ensure a little more for the `_jumpto_stack` stack frame
                    // clang tends to optimize out a bare `alloca` call so we need to 
                    //  ensure it sees it as live; we store it in a local and pass that to `_jumpto_stack`
    char* no_opt = NULL;
    if (extra > 0) {
      no_opt = lh_alloca(extra); // allocate room on the stack; in here the new stack will get copied.
    }
    // since we allocated more, the execution of `_jumpto_stack` will be in a stack frame 
    // that will not get overwritten itself when copying the new stack
    _jumpto_stack(cs->frames, cs->size, (byte*)cstack_base(cs), entry, freecframes, no_opt);
  }
}

// jump to a continuation a result 
static __noinline __noreturn void jumpto_resume( resume* r, lh_value local, lh_value arg )
{
  r->hstack.hframes[0].kind.eff.local = local; // write directly so it gets restored with the new local
  r->arg = arg; // set the argument in the cont slot  
  jumpto(&r->cstack, &r->hstack, &r->entry, false);
}

// jump to a continuation a result 
static __noinline __noreturn void jumpto_fragment(fragment* f, lh_value res)
{
  f->res = res; // set the argument in the cont slot  
  jumpto(&f->cstack, NULL, &f->entry, false);
}



/*-----------------------------------------------------------------
  Capture stack
-----------------------------------------------------------------*/

// Copy part of the C stack into a context.
static void capture_cstack(cstack* cs, const void* bottom, const void* top)
{
  ptrdiff_t size = stack_diff(top, bottom);
  if (size <= 0) { // (stackdown ? top >= bottom : top <= bottom) {
    // top is not above bottom; don't capture the stack
    cs->base = bottom;
    cs->size = 0;
    cs->frames = NULL;
  }
  else {
    // copy the stack 
    cs->base = (bottom <= top ? bottom : top); // always lowest address
    cs->size = size;
    cs->frames = checked_malloc(size);
    memcpy(cs->frames, cs->base, size);
  }
}

// Capture part of a handler stack.
static void capture_hstack(hstack* hs, hstack* to, handler* h ) {
  count_t hidx = (h - hs->hframes);
  assert(hidx < hs->count);
  assert(&hs->hframes[hidx] == h);
  hstack_init(to);
  hstack_append_copyfrom(to, hs, hidx);  
}

/*-----------------------------------------------------------------
    Yield to handler
-----------------------------------------------------------------*/

// Return to a handler by unwinding the handler stack.
static void __noreturn yield_to_handler(hstack* hs, handler* h,
  resume* resume, const lh_operation* op, lh_value oparg)
{
  assert(is_effhandler(h));
  cstack cs;
  hstack_pop_upto(hs, h, &cs);
  h->kind.eff.arg = oparg;
  h->kind.eff.arg_op = op;
  h->kind.eff.arg_resume = resume;
  jumpto(&cs, NULL, &h->kind.eff.entry, true);
}

/*-----------------------------------------------------------------
  Captured resume & yield  
-----------------------------------------------------------------*/

// Call a `resume* r`. First capture a jump point and c-stack into a `fragment`
// and push it in a fragment handler so the resume will return here later on.
static __noinline lh_value capture_resume_call(hstack* hs, resume* r, lh_value resumelocal, lh_value resumearg) {
  // initialize continuation
  fragment* f = checked_malloc(sizeof(fragment));
  f->refcount = 1;
  f->res = lh_value_null; 
  #ifdef _STATS
  stats.rcont_captured_fragment++;
  #endif    
  // and set our jump point
  if (_lh_setjmp(f->entry) != 0) {
    // longjmp back from the resume
    lh_value res = f->res; // get result
    #ifdef _STATS
    stats.rcont_resumed_fragment++;
    #endif
    // pop our special "fragment" frame
    assert(hs == &__hstack);
    assert(hs->count > 0);
    assert(is_fragmenthandler(hstack_top(hs)));
    assert(hstack_top(hs)->kind.frag.fragment == f);
    hstack_pop(hs);
    // return the result of the resume call     
    return res;
  }
  else {
    // we set our jump point; now capture the stack upto the stack base of the continuation 
    void* top = get_cstack_top();
    capture_cstack(&f->cstack, cstack_bottom(&r->cstack), top);
    #ifdef _STATS
    if (f->cstack.frames == NULL) stats.rcont_captured_empty++;
    stats.rcont_captured_size += (long)f->cstack.size;
    #endif
    // push a special "fragment" frame to remember to restore the stack when yielding to a handler across non-scoped resumes
    hstack_push_fragment(hs, f);
    // and now jump to the entry with resume arg
    jumpto_resume(r, resumelocal, resumearg);
  }
}

// Capture a first-class resumption and yield to the handler.
static __noinline lh_value capture_resume_yield(hstack* hs, handler* h, const lh_operation* op, lh_value oparg )
{
  // initialize continuation
  resume* r = checked_malloc(sizeof(resume));
  r->lhresume.rkind = FullResume;
  r->refcount = 1;
  r->arg = lh_value_null;
  #ifdef _STATS
  stats.rcont_captured_resume++;
  #endif    
  // and set our jump point
  if (_lh_setjmp(r->entry) != 0) {
    // longjmp back here when the resumption is called
    assert(hs == &__hstack);
    lh_value res = r->arg;
    #ifdef _STATS
    stats.rcont_resumed_resume++;
    #endif
    // release our context
    resume_release(r);
    // return the result of the resume call
    return res;
  }
  else {
    // we set our jump point; now capture the stack upto the handler
    void* top = get_cstack_top();
    assert(is_effhandler(h));
    capture_cstack(&r->cstack, h->kind.eff.stackbase, top);
    // capture hstack
    capture_hstack(hs, &r->hstack, h);
    #ifdef _STATS
    if (r->cstack.frames == NULL) stats.rcont_captured_empty++;
    stats.rcont_captured_size += (long)r->cstack.size + (long)r->hstack.size * sizeof(handler);
    #endif
    assert(h->kind.eff.hdef == r->hstack.hframes[0].kind.eff.hdef); // same handler?
    // and yield to the handler
    yield_to_handler(hs, h, r, op, oparg);
  }
}



/*-----------------------------------------------------------------
   Handle
-----------------------------------------------------------------*/

// Start a handler 
static __noinline lh_value handle_with(hstack* hs, handler* h, lh_value(*action)(lh_value), lh_value arg )
{
  // set the handler entry point 
  assert(is_effhandler(h));
  #ifndef NDEBUG
  const lh_handlerdef* hdef = h->kind.eff.hdef;
  void* base = h->kind.eff.stackbase;
  #endif
  if (_lh_setjmp(h->kind.eff.entry) != 0) {
    // needed as some compilers optimize wrongly (e.g. gcc v5.4.0 x86_64 with -O2 on msys2)
    hs = &__hstack;      
    assert(hs->count > 0);
    // we yielded back to the handler; the `handler->arg` is filled in.
    // note: if we return trough non-scoped resumes the handler stack may be
    // different and handler `h` will point to a random handler in that stack!
    // ie. we need to load from the top of the current handler stack instead.
    // This is also necessary if the handler stack was reallocated to grow.
    h = hstack_top(hs);  // re-load our handler
    #ifndef NDEBUG
    assert(hdef == h->kind.eff.hdef);
    assert(base == h->kind.eff.stackbase);
    #endif
    lh_value  res    = h->kind.eff.arg;
    lh_value  local  = h->kind.eff.local;
    resume*   resume = h->kind.eff.arg_resume;
    const lh_operation* op = h->kind.eff.arg_op;
    assert(op == NULL || op->optag->effect == h->effect);
    hstack_pop(hs);
    if (op != NULL && op->opfun != NULL) {
      // push a scoped frame if necessary
      if (op->opkind==LH_OP_SCOPED) {
        hstack_push_scoped(hs,resume);
      }
      assert((void*)&resume->lhresume == (void*)resume);
      // and call the operation handler
      res = op->opfun(&resume->lhresume, local, res);
      // pop our scoped frame (potentially releasing the resumption)
      if (op->opkind==LH_OP_SCOPED) {
        assert(hs==&__hstack);
        hstack_pop(hs);
      }
    }
    return res;
  }
  else {
    // we set up the handler, now call the action 
    lh_value res = action(arg);
    assert(hs==&__hstack);
    assert(hs->count > 0);
    h = hstack_top(hs);  // re-load our handler since the handler stack could have been reallocated
    #ifndef NDEBUG
    assert(hdef == h->kind.eff.hdef);
    assert(base == h->kind.eff.stackbase);
    #endif
    // pop our handler
    lh_resultfun* resfun = h->kind.eff.hdef->resultfun;
    lh_value local = h->kind.eff.local;
    hstack_pop(hs);
    if (resfun != NULL) {
      res = resfun(local, res);
    }
    return res;
  }
}

// `handle_upto` installs a handler on the stack with a given stack `base`. 
static __noinline 
#if defined(__GNUC__) && defined(LH_ABI_x86) 
__noopt
#endif
lh_value handle_upto( hstack* hs, void* base, const lh_handlerdef* def,
                      lh_value local, lh_value(*action)(lh_value), lh_value arg)
{
  // allocate handler frame on the stack so it will be part of a captured continuation
  handler* h = hstack_push_effect(hs,def,base,local);
  lh_value res = handle_with(hs, h, action, arg);
  // after returning, check if there is a fragment frame we should jump to..
  hs = &__hstack;
  if (hs->count > 0) {
    h = hstack_top(hs);
    if (is_fragmenthandler(h)) {
      jumpto_fragment(h->kind.frag.fragment, res);
    }
  }
  // otherwise just return normally
  return res;
}


// `handle` installs a new handler on the stack and calls the given `action` with argument `arg`.
lh_value lh_handle( const lh_handlerdef* def, lh_value local, lh_actionfun* action, lh_value arg)
{
  hstack* hs = &__hstack;
  bool init = lh_init(hs);
  void* base = get_cstack_top();
  lh_value res = handle_upto(hs, base, def, local, action, arg);
  if (init) lh_done(hs);
  return res;
}


/*-----------------------------------------------------------------
  Yield an operation
-----------------------------------------------------------------*/

// `yieldop` yields to the first enclosing handler that can handle
//   operation `optag` and passes it the argument `arg`.
static lh_value __noinline yieldop(lh_optag optag, lh_value arg)
{
  // find the operation handler along the handler stack
  hstack*   hs = &__hstack;
  count_t   skipped;
  const lh_operation* op;
  lh_value  local;
  handler* h = hstack_find(hs, optag, &op, &local, &skipped);
  if (op->opkind <= LH_OP_NORESUME) {
    yield_to_handler(hs, h, NULL, op, arg);
  }
  // push a special "skip" handler; when the operation function calls operations itself,
  // those will not be handled by any handlers above handler.
  else if (op->opkind <= LH_OP_TAIL) {
    // OP_TAIL_NOOP: will not call operations so no need for a skip frame
    // call the operation function and return directly (as it promised to tail resume)
    count_t  hidx = 0;
    // push a skip frame
    if (op->opkind != LH_OP_TAIL_NOOP) {
      hidx = hstack_indexof(hs, h);
      hstack_push_skip(hs,skipped+1);
    }
    // setup up a stack allocated tail resumption
    tailresume r;
    r.lhresume.rkind = TailResume;
    r.local = local;
    r.resumed = false;
    assert((void*)(&r.lhresume) == (void*)&r);
    // call the operation handler directly for a tail resumption
    lh_value res = op->opfun(&r.lhresume, local, arg);
    // pop our skip handler
    if (op->opkind != LH_OP_TAIL_NOOP) {
      hstack_pop_skip(hs);
      h = hstack_at(hs, hidx);
    }
    // if we returned from a `lh_tail_resume` we just return its result
    if (r.resumed) {
      h->kind.eff.local = r.local;
      return res;
    }
    // otherwise no resume was called; yield back to the handler with the result.
    else {
      yield_to_handler(hs, h, NULL, NULL, res);
    }
  }
  else {
    // In general, capture a resumption and yield to the handler
    return capture_resume_yield(hs, h, op, arg);
  }
  assert(false);
  return lh_value_null;
}

// Yield to the first enclosing handler that can handle
// operation `optag` and pass it the argument `arg`.
lh_value lh_yield(lh_optag optag, lh_value arg) {
  #ifdef _DEBUG_STATS
  stats.operations++;
  #endif
  return yieldop(optag, arg);
}

// Empty yield args is allocated statically
static const yieldargs yargs_null = { 0, { lh_value_null } };

// Yield N arguments to an operation
lh_value lh_yieldN(lh_optag optag, int argcount, ...) {
  assert(argcount >= 0);
  if (argcount <= 0) {
    return lh_yield(optag, lh_value_yieldargs(&yargs_null));
  }
  else {
    va_list ap;
    va_start(ap, argcount);
    yieldargs* yargs = checked_malloc(sizeof(yieldargs) + ((argcount - 1) * sizeof(lh_value)));
    yargs->argcount = argcount;
    for (int i = 0; i < argcount; i++) {
      yargs->args[i] = va_arg(ap, lh_value);
    }
    va_end(ap);
    lh_value res = lh_yield(optag, lh_value_yieldargs(yargs));
    checked_free(yargs);
    return res;
  }
}


/*-----------------------------------------------------------------
  Resume
-----------------------------------------------------------------*/

// Cast to a first class resumption.
static resume* to_resume(lh_resume r) {
  if (r->rkind != FullResume) fatal(EINVAL,"Trying to generally resume a tail-resumption");
  return (resume*)r;
}

static lh_value lh_release_resume_(resume* r, lh_value local, lh_value res) {
  return capture_resume_call(&__hstack, r, local, res);
}

lh_value lh_release_resume(lh_resume r, lh_value local, lh_value res) {
  return lh_release_resume_(to_resume(r), local, res);
}

lh_value lh_do_resume(lh_resume r, lh_value local, lh_value res) {
  return lh_release_resume_(resume_acquire(to_resume(r)), local, res);
}

void lh_release(lh_resume r) {
  if (r->rkind==FullResume) resume_release((resume*)r);
}

lh_value lh_tail_resume(lh_resume r, lh_value local, lh_value res) {
  if (r->rkind==TailResume) {
    tailresume* tr = (tailresume*)(r);
    tr->resumed = true;
    tr->local = local;
    return res;
  }
  else {
    return lh_release_resume(r, local, res);
  }
}

lh_value lh_scoped_resume(lh_resume r, lh_value local, lh_value res) {
  return lh_do_resume(r, local, res);
}

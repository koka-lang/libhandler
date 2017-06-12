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

#include <stddef.h>   // ptrdiff_t
#include <stdint.h>   // intptr_t
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
typedef ptrdiff_t     count;   // signed natural machine word

// forward declarations
struct _handler;
typedef struct _handler handler;

// A handler stack; Separate from the C-stack so it can be searched even if the C-stack contains fragments
// Handler frames are variable size so we use a `byte*` for the frames.
// Also, we use relative addressing (using `handler::prev`) such that an `hstack` can be reallocated
// and copied freely.
typedef struct _hstack {
  handler*           top;       // top of the handlers `hframes <= top < hframes+count` 
  count              count;     // number of bytes in use in `hframes`
  count              size;      // size in bytes
  byte*              hframes;   // array of handlers (0 is bottom frame)
} hstack;

// A captured C stack
typedef struct _cstack {           
  const void*        base;      // The `base` is the lowest/smallest adress of where the stack is captured
  count              size;      // The byte size of the captured stack
  byte*              frames;    // The captured stack data (allocated in the heap)
} cstack;


// A `fragment` is a captured C-stack and an `entry`.
typedef struct _fragment {
  lh_jmp_buf         entry;     // jump powhere the fragment was captured
  cstack             cstack;    // the captured c stack 
  count              refcount;  // fragments are allocated on the heap and reference counted.
  volatile lh_value  res;       // when jumped to, a result is passed through `res`
} fragment;

// Operation handlers receive an `lh_resume*`; the kind determines what it points to.
typedef enum _resumekind {
  FullResume,       // `lh_resume` is a `resume`
  TailResume        // `lh_resume` is a `tailresume`
} resumekind;

// Typedef'ed to `lh_resume` in the header. 
// This is an algebraic data type and is either a `resume` or `tailresume`.
// The `_lh_resume` should be the first field of those (so we can upcast safely).
struct _lh_resume {
  resumekind         rkind;       // the resumption kind
};

// Every resume kind starts with an `lhresume` field (for safe upcasting)
#define to_lhresume(r) (&(r)->lhresume)

// A first-class resumption
typedef struct _resume {
  struct _lh_resume  lhresume;    // contains the kind: always `FullResume` (must be first field, used for casts)
  count              refcount;    // resumptions are heap allocated
  lh_jmp_buf         entry;       // jump point where the resume was captured
  cstack             cstack;      // captured cstack
  hstack             hstack;      // captured hstack  always `size == count`
  volatile lh_value  arg;         // the argument to `resume` is passed through `arg`.
} resume;

// An optimized resumption that can only used for tail-call resumptions (`lh_tail_resume`).
typedef struct _tailresume {
  struct _lh_resume  lhresume;    // the kind: always `TailResume` (must be first field, used for casts)
  volatile lh_value  local;       // the new local value for the handler
  volatile bool      resumed;     // set to `true` if `lh_tail_resume` was called
} tailresume;



// A handler; there are four kinds of frames
// 1. normal effect handlers, pushed when a handler is called
// 2. a "fragment" handler: these are pushed when a first-class continuation is
//    resumed through `lh_resume` or `lh_release_resume`. Such resume may overwrite parts of the
//    current stack which is saved in its own `fragment` continuation. 
// 3. a "scoped" handler: these are pushed when a `LH_OP_SCOPED` operation is executed
//    to automatically release the resumption function when the scope is exited.
// 4. a "skip" handler: used to skip a number of handler frames for tail call resumptions.
struct _handler {
  lh_effect   effect;               // The effect that is handled (fragment, skip, and scoped handlers have their own effect)
  count       prev;                 // the handler below on the stack is `prev` bytes before this one
};

// Every handler type starts with a handler field (for safe upcasting)
#define to_handler(h) (&(h)->handler)

// The special handlers are identified by these effects.
LH_DEFINE_EFFECT0(__fragment)
LH_DEFINE_EFFECT0(__scoped)
LH_DEFINE_EFFECT0(__skip)


// Regular effect handler.
typedef struct _effecthandler {
  handler              handler;
  lh_jmp_buf           entry;       // used to jump back to a handler 
  const lh_handlerdef* hdef;        // operation definitions
  volatile lh_value    arg;         // the yield argument is passed here
  const lh_operation*  arg_op;      // the yielded operation is passed here
  resume*              arg_resume;  // the resumption function for the yielded operation
  void*                stackbase;   // pointer to the c-stack just below the handler
  lh_value             local;       
} effecthandler;

// A skip handler.
typedef struct _skiphandler {
  handler              handler;
  count                toskip;      // when looking for an operation handler, skip the next `toskip` bytes.
} skiphandler;

// A fragment handler just contains a `fragment`.
typedef struct _fragmenthandler {
  handler              handler;
  fragment*            fragment;
} fragmenthandler;

// A scoped handler keeps track of the resumption in the scope of
// an operator so it can be released properly.
typedef struct _scopedhandler {
  handler              handler;
  resume*              resume;
} scopedhandler;


// thread local `__hstack` is the 'shadow' handler stack
__thread hstack __hstack = { NULL, 0, 0, NULL };


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


// approximate the top of the stack -- conservatively upward
static __noinline __noopt void* get_stack_top() {
  auto byte* top = (byte*)&top;
  return top;
}

// true if the stack grows up
static bool stackup = false;

#ifndef NDEBUG
// base of our c stack
static const void* stackbottom = NULL;
#endif

// infer the direction in which the stack grows and the size of a stack frame 
static __noinline __noopt void infer_stackdir() {
  auto void* mark = (void*)&mark;
  void* top = get_stack_top();
  stackup = (mark < top);
  #ifndef NDEBUG
  stackbottom = mark;
  #endif
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
// Does this pointer point to the C stack?
static bool in_cstack(const void* p) {
  const void* top = get_stack_top();
  return !(stack_isbelow(top, p) || stack_isbelow(p, stackbottom));
}

// In debug mode, check we don't pass pointers to the C stack in `lh_value`s.
lh_value lh_value_ptr(const void* p) {
  if (in_cstack(p)) fatal(EINVAL,"Cannot pass pointers to the c-stack in a lh_value");
  return ((lh_value)((intptr_t)p));
}
#endif


/*-----------------------------------------------------------------
  Effect and optag names
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
  count rcont_captured_size;

  long rcont_resumed_scoped;
  long rcont_resumed_resume;
  long rcont_resumed_fragment;
  long rcont_resumed_tail;
  
  long rcont_released;
  count rcont_released_size;

  long operations;
  count hstack_max;
} stats = {
    0, 0, 0, 0, 0,
    0, 0, 0, 
    0, 0,
    0, 0, 
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
    fprintf(h, "  hstack max  :%li kb\n", (long)(stats.hstack_max + 1023) /1024);
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
  count captured = stats.rcont_captured_scoped + stats.rcont_captured_resume + stats.rcont_captured_fragment; 
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


// Pointer difference in bytes
static ptrdiff_t ptrdiff(const void* p, const void* q) {
  return (byte*)p - (byte*)q;
}



/*-----------------------------------------------------------------
  Fragments
-----------------------------------------------------------------*/

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
// Forward
static void hstack_free(ref hstack* hs);

// release a resumptions; returns `true` if it was released
static __noinline void _resume_free(resume* r) {
  #ifdef _STATS
  stats.rcont_released++;
  stats.rcont_released_size += (long)r->cstack.size + (long)r->hstack.size;
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
  if (r->refcount == 0) _resume_free(r);
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
static bool is_effecthandler(const handler* h) {
  return (!is_fragmenthandler(h) && !is_scopedhandler(h) && !is_skiphandler(h));
}

static count handler_size(const lh_effect effect) {
  if (effect == LH_EFFECT(__skip)) return sizeof(skiphandler);
  else if (effect == LH_EFFECT(__fragment)) return sizeof(fragmenthandler);
  else if (effect == LH_EFFECT(__scoped)) return sizeof(scopedhandler);
  else return sizeof(effecthandler);
}
#endif

// Return the handler below on the stack
static handler* _handler_prev(const handler* h) {
  assert(h->prev >= 0); // may be equal to zero, in which case the same handler is returned! (bottom frame)
  return (handler*)((byte*)h - h->prev);
}

// Return a pointer to the last skipped handler 
static handler* _handler_prev_skip(const skiphandler* sh) {
  assert(sh->toskip > 0);
  return (handler*)((byte*)sh - sh->toskip);
}


// Increase the reference count of handler fields
static handler* handler_acquire(handler* h) {
  if (is_fragmenthandler(h)) {
    fragment_acquire(((fragmenthandler*)h)->fragment);
  }
  else if (is_scopedhandler(h)) {
    resume_acquire(((scopedhandler*)h)->resume);
  }
  return h;
}

// Decrease the reference count of handler fields
static void handler_release(ref handler* h) {
  if (is_fragmenthandler(h)) {
    fragment_release_at(&((fragmenthandler*)h)->fragment);
  }
  else if (is_scopedhandler(h)) {
    resume_release_at(&((scopedhandler*)h)->resume);
  }
}


/*-----------------------------------------------------------------
  Handler stacks
-----------------------------------------------------------------*/

// Handler stacks increase exponentially in size up to a limit, then increase linearly
#define HMINSIZE     (32*sizeof(effecthandler))
#define HMAXEXPAND   (2*1024*1024)

static count hstack_goodsize(count needed) {
  if (needed > HMAXEXPAND) {
    return (HMAXEXPAND * ((needed + HMAXEXPAND - 1) / HMAXEXPAND)); // round up to next HMAXEXPAND
  }
  else {
    count newsize;
    for (newsize = HMINSIZE; newsize < needed; newsize *= 2) {}  // round up to next power of 2
    return newsize;
  }
}

// forward
static handler* hstack_at(const hstack* hs, count  idx);

// Initialize a handler stack
static void hstack_init(hstack* hs) {
  hs->count = 0;
  hs->size = 0;
  hs->hframes = NULL;
  hs->top = hstack_at(hs, 0);
}


// Current top handler frame
static handler* hstack_top(const hstack* hs) {
  return hs->top;
}

// Bottom handler
static handler* hstack_bottom(const hstack* hs) {
  return (handler*)hs->hframes;
}

// Is the  handler stack empty?
static bool hstack_empty(const hstack* hs) {
  return (hs->count <= 0);
}



#ifndef NDEBUG
static bool hstack_contains(const hstack* hs, const handler* h) {
  return (h != NULL && hs->count > 0 && hstack_bottom(hs) <= h && h <= hstack_top(hs));
}

static bool valid_handler(const hstack* hs, const handler* h) {
  return (h != NULL && hstack_contains(hs, h) &&
          (h->prev==0 || h->prev == handler_size(_handler_prev(h)->effect)));
}

static bool hstack_follows(const hstack* hs, const handler* h, const handler* g) {
  assert(valid_handler(hs, h));
  assert(valid_handler(hs, g));
  return (h != g && (byte*)h == (byte*)g - g->prev);
}
#endif


// Return the number of bytes between a handler and the end of the handler stack
static count  hstack_indexof(const hstack* hs, const handler* h) {
  assert((hs->count==0 && h==hs->top) || valid_handler(hs,h));
  return (hs->count - ptrdiff(h, hs->hframes));
}

// Return the handler that is `idx` bytes down the handler stack
static handler* hstack_at(const hstack* hs, count  idx) {
  assert(idx >= 0 && idx <= hs->count);
  return (handler*)(&hs->hframes[hs->count - idx]);
}

// Size of the handler on top
static count hstack_topsize(const hstack* hs) {
  return hstack_indexof(hs,hs->top);
}

// Reallocate the hstack
static void hstack_realloc_(ref hstack* hs, count needed) {
  count newsize = hstack_goodsize(needed);
  count topsize = hstack_topsize(hs);
  hs->hframes = checked_realloc(hs->hframes, newsize);
  hs->size = newsize;
  hs->top = hstack_at(hs, topsize);
  #ifdef _STATS
  if (newsize > stats.hstack_max) stats.hstack_max = newsize;
  #endif
}

// Return the previous handler, or NULL if at the bottom frame
static handler* hstack_prev(hstack* hs, handler* h) {
  assert(valid_handler(hs, h));
  handler* prev = _handler_prev(h);
  assert(prev==h || hstack_follows(hs,prev,h));
  return (prev==h ? NULL : prev);
}

// Return the bottomost skipped handler
static handler* hstack_prev_skip(hstack* hs, skiphandler* h) {
  assert(valid_handler(hs, to_handler(h)));
  handler* prev = _handler_prev_skip(h);  
  assert(valid_handler(hs, prev));
  return prev;
}


// Release the handler frames of an `hstack`
static void hstack_free(ref hstack* hs) {
  assert(hs != NULL);
  if (hs->hframes != NULL) {
    if (!hstack_empty(hs)) {
      handler* h = hstack_top(hs);
      do {
        handler_release(h);
        h = hstack_prev(hs, h);
      } 
      while (h != NULL);
    }
    checked_free(hs->hframes);
    hs->size = 0;
    hs->hframes = NULL;
  }
}



/*-----------------------------------------------------------------
  pop and push
-----------------------------------------------------------------*/


// Pop a handler frame, decreasing its reference counts.
static void hstack_pop(ref hstack* hs) {
  assert(!hstack_empty(hs));
  handler_release(hstack_top(hs));
  hs->count = ptrdiff(hs->top, hs->hframes);
  hs->top = _handler_prev(hs->top);
}

// Pop a skip frame
static void hstack_pop_skip(ref hstack* hs) {
  assert(!hstack_empty(hs));
  assert(is_skiphandler(hstack_top(hs)));
  hs->count = ptrdiff(hs->top, hs->hframes);
  hs->top = _handler_prev(hs->top);
}

// Pop a fragment frame
static fragment* hstack_pop_fragment(hstack* hs) {
  if (!hstack_empty(hs)) {
    handler* h = hstack_top(hs);
    if (is_fragmenthandler(h)) {
      fragment* fragment = fragment_acquire(((fragmenthandler*)h)->fragment);
      hstack_pop(hs);
      return fragment;
    }
  }
  return NULL;
}


// Ensure the handler stack is big enough for `extracount` handlers.
static handler* hstack_ensure_space(ref hstack* hs, count extracount) {
  count needed = hs->count + extracount;
  if (needed > hs->size) {
    hstack_realloc_(hs, needed);
  }
  return hstack_at(hs, 0);
}


// Push a new uninitialized handler frame and return a reference to it.
static handler* _hstack_push(ref hstack* hs, lh_effect effect, count size) {
  assert(size == handler_size(effect));
  handler* h = hstack_ensure_space(hs, size);
  h->effect = effect;
  h->prev = ptrdiff(h, hs->top);
  assert((hs->count > 0 && h->prev > 0) || (hs->count == 0 && h->prev == 0));
  hs->top = h;
  hs->count += size;
  return h;
}

// Push an effect handler
static effecthandler* hstack_push_effect(ref hstack* hs, const lh_handlerdef* hdef, void* stackbase, lh_value local)
{
  effecthandler* h = (effecthandler*)_hstack_push(hs, hdef->effect, sizeof(effecthandler));
  h->hdef = hdef;
  h->stackbase = stackbase;
  h->local = local;
  h->arg = lh_value_null;
  h->arg_op = NULL;
  h->arg_resume = NULL;
  return h;
}

// Push a skip handler
static skiphandler* hstack_push_skip(ref hstack* hs, count toskip) {
  skiphandler* h = (skiphandler*)_hstack_push(hs, LH_EFFECT(__skip), sizeof(skiphandler));
  h->toskip = toskip;
  return h;
}

// Push a fragment handler
static fragmenthandler* hstack_push_fragment(ref hstack* hs, fragment* fragment) {
  fragmenthandler* h = (fragmenthandler*)_hstack_push(hs, LH_EFFECT(__fragment), sizeof(fragmenthandler));
  h->fragment = fragment;
  return h;
}

// Push a scoped handler
static scopedhandler* hstack_push_scoped(ref hstack* hs, resume* resume) {
  scopedhandler* h = (scopedhandler*)_hstack_push(hs, LH_EFFECT(__scoped), sizeof(scopedhandler));
  h->resume = resume;
  return h;
}


static void _hstack_append_movefrom(ref hstack* hs, ref hstack* topush, const handler* from) {
  assert(hstack_contains(topush, from));
  count  needed = hstack_indexof(topush, from);
  handler* bot = hstack_ensure_space(hs, needed);
  memcpy(bot, from, needed);
  bot->prev = hstack_topsize(hs);
  hs->count += needed;
  hs->top = hstack_at(hs,hstack_topsize(topush));
}

// Copy handlers from one stack to another increasing reference counts as appropiate.
// Include `from` in the copied handlers.
static void hstack_append_copyfrom(ref hstack* hs, ref hstack* tocopy, handler* from) {
  assert(hstack_contains(tocopy,from));
  _hstack_append_movefrom(hs, tocopy, from);
  handler* h = hstack_top(tocopy);
  do {
    handler_acquire(h);    
    h = hstack_prev(tocopy,h);
  } 
  while (h != NULL && h >= from);
}


// Find an operation that handles `optag` in the handler stack.
static effecthandler* hstack_find(ref hstack* hs, lh_optag optag, out const lh_operation** op, out lh_value* local, out count* skipped) {
  if (!hstack_empty(hs)) {
    handler* h = hstack_top(hs);
    do {
      assert(valid_handler(hs, h));
      if (h->effect == optag->effect) {
        effecthandler* eh = (effecthandler*)h;
        assert(eh->hdef != NULL);
        const lh_operation* oper = &eh->hdef->operations[optag->opidx];
        assert(oper->optag == optag); // can fail if operations are defined in a different order than declared
        if (oper->opfun != NULL) {
          *skipped = hstack_indexof(hs, h); assert(*skipped > 0);
          *op = oper;
          *local = eh->local;
          return eh;
        }
      }
      else if (is_skiphandler(h)) {
        h = hstack_prev_skip(hs,(skiphandler*)h);
      }
      h = hstack_prev(hs, h);
    } while (h != NULL);
  }
  fatal(ENOSYS, "no handler for operation found: '%s'", lh_optag_name(optag));
  *skipped = 0;
  *op = NULL;
  *local = lh_value_null;
  return NULL;
}




/*-----------------------------------------------------------------
  Unwind a handler stack
  This is a bit involved since it requires popping one frame at a
  time decreasing reference counts, but also when a "fragment" handler
  is encountered we need to restore its saved stack. We cannot do that
  right away though as that may overwrite our own stack frame. Therefore,
  the unwinding returns a `cstack` object that should be restored when
  possible. 

  Todo:  we could optimize more by first scanning the maximum
  stack we need and allocate only once; now we reallocate on every
  newly extending fragment. In practice though it is rare to encounter
  more than one fragment so this may not be worth it.
-----------------------------------------------------------------*/
const byte* _min(const byte* p, const byte* q) { return (p <= q ? p : q); }
const byte* _max(const byte* p, const byte* q) { return (p >= q ? p : q); }

// Extend cstack `cs` in-place to encompass both the `ds` stack and itself.
static void cstack_extendfrom(ref cstack* cs, ref cstack* ds, bool will_free_ds) {
  const byte* csb = cstack_base(cs);
  const byte* dsb = cstack_base(ds);
  if (cs->frames == NULL) {
    // nothing yet, just copy `ds`
    if (ds->frames != NULL) {
      if (will_free_ds) {
        // `ds` is about to be freed.. take over its frames
        *cs = *ds; // copy fields
        // and prevent freeing `ds`
        ds->frames = NULL;
        ds->size = 0;
      }
      else {
        // otherwise copy the c-stack from ds
        cs->frames = checked_malloc(ds->size);
        memcpy(cs->frames, ds->frames, ds->size);
        cs->base = ds->base;
        cs->size = ds->size;
      }
    }
  }
  else {
    // otherwise extend such that we can merge `cs` and `ds` together
    const byte* newbase = _min(csb,dsb);
    ptrdiff_t   newsize = _max(csb + cs->size, dsb + ds->size) - newbase;
    // check if we need to reallocate; no need if `ds` fits right in.
    if (csb != newbase || cs->size != newsize) {
      // reallocate..
      byte* newframes = checked_malloc(newsize);
      // if non-overlapping, copy the current stack first into the gap
      // (there is never a gap at the ends as `cs` or `ds` either start or end the `newframes`).
      if ((dsb > csb + cs->size) || (dsb + ds->size < csb)) {
        // todo: we could optimize this further by just copying the gap part
        memcpy(newframes, newbase, newsize);
      }
      // next copy the cs->frames into the new frames
      assert(csb >= newbase);
      assert(csb + cs->size <= newbase + newsize);
      memcpy(newframes + (csb - newbase), cs->frames, cs->size);
      // and update cs
      checked_free(cs->frames);
      cs->frames = newframes;
      cs->size = newsize;
      cs->base = newbase;
    }
    // and finally copy the new `ds->frames` into `cs` (which is now large enought to contain `ds`)
    assert(cs->base == newbase);
    assert(cs->size == newsize);
    assert(dsb >= newbase);
    assert(dsb + ds->size <= newbase + newsize);
    memcpy(cs->frames + (dsb - newbase), ds->frames, ds->size);    
  }
}


// Pop the stack up to the given handler `h` (which should reside in `hs`)
// Return a stack object in `cs` (if not `NULL) that should be restored later on.
static void hstack_pop_upto(ref hstack* hs, ref handler* h, out cstack* cs) 
{
  if (cs != NULL) cstack_init(cs);
  assert(!hstack_empty(hs));
  handler* cur = hstack_top(hs);
  handler* skip_upto = NULL;
  while( cur > h ) {
    if (skip_upto != NULL) {
      if (cur==skip_upto) skip_upto = NULL;
      else if (cur < skip_upto) fatal(EFAULT, "handler stack is invalid");
    }
    else {
      if (is_fragmenthandler(cur)) {
        // special "fragment" handler; remember to restore the stack
        fragment* f = ((fragmenthandler*)cur)->fragment;
        if (f->cstack.frames != NULL) {
          cstack_extendfrom(cs, &f->cstack, f->refcount == 1);
        }
      }
      else if (is_skiphandler(cur)) {
        skip_upto = hstack_prev_skip(hs,(skiphandler*)cur);
        assert(valid_handler(hs, skip_upto));
      }
    }
    hstack_pop(hs);
    cur = hstack_top(hs);
  }
  assert(cur == h);
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
  assert(hs == &__hstack && hs->size>0 && hs->count==0 && (byte*)hs->top==&hs->hframes[0]);
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
    hstack_append_copyfrom(&__hstack, hs, hstack_bottom(hs));
  }
  if (cs->frames == NULL) {
    // if no stack, just jump back down the stack; 
    // sanity: check if the entry is really below us!
    void* top = get_stack_top();
    if (cs->base != NULL && stack_isbelow(top,cstack_top(cs))) {
      fatal(EFAULT,"Trying to jump up the stack to a scope that was already exited!");
    }
    // long jump back up direcly, no need to restore stacks
    _lh_longjmp(*entry, 1);
  }
  else {
    // ensure there is enough room on the stack; 
    void* top = get_stack_top();
    ptrdiff_t extra = stack_diff(cstack_top(cs), top);                     
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
  handler* h = hstack_bottom(&r->hstack);
  assert(is_effecthandler(h));
  ((effecthandler*)h)->local = local; // write directly so it gets restored with the new local
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

// Capture part of a handler stack (includeing h).
static void capture_hstack(hstack* hs, hstack* to, effecthandler* h ) {
  hstack_init(to);
  hstack_append_copyfrom(to, hs, to_handler(h));  
}

/*-----------------------------------------------------------------
    Yield to handler
-----------------------------------------------------------------*/

// Return to a handler by unwinding the handler stack.
static void __noinline __noreturn yield_to_handler(hstack* hs, effecthandler* h,
  resume* resume, const lh_operation* op, lh_value oparg)
{
  cstack cs;
  hstack_pop_upto(hs, to_handler(h), &cs);
  h->arg = oparg;
  h->arg_op = op;
  h->arg_resume = resume;
  jumpto(&cs, NULL, &h->entry, true);
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
    // release our fragment
    fragment_release(f);
    // return the result of the resume call     
    return res;
  }
  else {
    // we set our jump point; now capture the stack upto the stack base of the continuation 
    void* top = get_stack_top();
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
static __noinline lh_value capture_resume_yield(hstack* hs, effecthandler* h, const lh_operation* op, lh_value oparg )
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
    void* top = get_stack_top();
    capture_cstack(&r->cstack, h->stackbase, top);
    // capture hstack
    capture_hstack(hs, &r->hstack, h);
    #ifdef _STATS
    if (r->cstack.frames == NULL) stats.rcont_captured_empty++;
    stats.rcont_captured_size += (long)r->cstack.size + (long)r->hstack.size;
    #endif
    assert(h->hdef == ((effecthandler*)(r->hstack.hframes))->hdef); // same handler?
    // and yield to the handler
    yield_to_handler(hs, h, r, op, oparg);
  }
}



/*-----------------------------------------------------------------
   Handle
-----------------------------------------------------------------*/
// Start a handler 
static __noinline lh_value handle_with(
    hstack* hs, effecthandler* h, lh_value(*action)(lh_value), lh_value arg )
{
  // set the handler entry point 
  #ifndef NDEBUG
  const lh_handlerdef* hdef = h->hdef;
  void* base = h->stackbase;
  #endif
  if (_lh_setjmp(h->entry) != 0) {
    // needed as some compilers optimize wrongly (e.g. gcc v5.4.0 x86_64 with -O2 on msys2)
    hs = &__hstack;      
    // we yielded back to the handler; the `handler->arg` is filled in.
    // note: if we return trough non-scoped resumes the handler stack may be
    // different and handler `h` will point to a random handler in that stack!
    // ie. we need to load from the top of the current handler stack instead.
    // This is also necessary if the handler stack was reallocated to grow.
    h = (effecthandler*)(hstack_top(hs));  // re-load our handler
    assert(is_effecthandler(to_handler(h)));
    #ifndef NDEBUG
    assert(hdef == h->hdef);
    assert(base == h->stackbase);
    #endif
    lh_value  res    = h->arg;
    lh_value  local  = h->local;
    resume*   resume = h->arg_resume;
    const lh_operation* op = h->arg_op;
    assert(op == NULL || op->optag->effect == h->handler.effect);
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
    h = (effecthandler*)hstack_top(hs);  // re-load our handler since the handler stack could have been reallocated
    #ifndef NDEBUG
    assert(hdef == h->hdef);
    assert(base == h->stackbase);
    #endif
    // pop our handler
    lh_resultfun* resfun = h->hdef->resultfun;
    lh_value local = h->local;
    hstack_pop(hs);
    if (resfun != NULL) {
      res = resfun(local, res);
    }
    return res;
  }
}

// `handle_upto` installs a handler on the stack with a given stack `base`. 
static __noinline 
#if defined(__GNUC__) && defined(LH_ABI_x86) && false
__noopt
#endif
lh_value handle_upto(hstack* hs, void* base, const lh_handlerdef* def,
  lh_value local, lh_value(*action)(lh_value), lh_value arg)
{
  // allocate handler frame on the stack so it will be part of a captured continuation
  effecthandler* h = hstack_push_effect(hs, def, base, local);
  lh_value res = handle_with(hs, h, action, arg);
  fragment* fragment = hstack_pop_fragment(hs);
  // after returning, check if there is a fragment frame we should jump to..
  if (fragment != NULL) {
    jumpto_fragment(fragment, res);
  }
  // otherwise just return normally
  return res;
}


// `handle` installs a new handler on the stack and calls the given `action` with argument `arg`.
lh_value lh_handle( const lh_handlerdef* def, lh_value local, lh_actionfun* action, lh_value arg)
{
  auto void* base = (void*)&base; // get_stack_top(); 
  hstack* hs = &__hstack;
  bool init = lh_init(hs);
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
  count     skipped;
  lh_value  local;
  const lh_operation* op;
  effecthandler* h = hstack_find(hs, optag, &op, &local, &skipped);

  // No resume (i.e. like `throw`)
  if (op->opkind <= LH_OP_NORESUME) {
    yield_to_handler(hs, h, NULL, op, arg);
  }
  
  // Tail resumptions
  else if (op->opkind <= LH_OP_TAIL) {
    // OP_TAIL_NOOP: will not call operations so no need for a skip frame
    // call the operation function and return directly (as it promised to tail resume)
    count  hidx = 0;
    // push a skip frame
    if (op->opkind != LH_OP_TAIL_NOOP) {
      hidx = hstack_indexof(hs, to_handler(h));
      hstack_push_skip(hs,skipped);
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
      h = (effecthandler*)hstack_at(hs, hidx);
      assert(is_effecthandler(to_handler(h)));
    }
    // if we returned from a `lh_tail_resume` we just return its result
    if (r.resumed) {
      h->local = r.local;
      return res;
    }
    // otherwise no resume was called; yield back to the handler with the result.
    else {
      yield_to_handler(hs, h, NULL, NULL, res);
    }
  }

  // In general, capture a resumption and yield to the handler
  else {
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
    // note: we need to use 'malloc' as we cannot pass arguments to an operation as a stack address
    // todo: perhaps we need to reserve more space in the handler to pass arguments?
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

lh_value lh_call_resume(lh_resume r, lh_value local, lh_value res) {
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
  return lh_call_resume(r, local, res);
}

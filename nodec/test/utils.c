#include "utils.h"
#include <assert.h>

/*-----------------------------------------------------------------------------
    divceil

        returns the smallest n such that n*b >= a
-----------------------------------------------------------------------------*/
static size_t divceil(size_t a, size_t b) {
  assert(b > 0);
  assert(a + b > a);          // no overflow
  return (a + b - 1) / b;
}

/*-----------------------------------------------------------------------------
    roundup

        returns the smallest multiple of b greater than or equal to a
                          b * ceil(a/b)
-----------------------------------------------------------------------------*/
size_t roundup(size_t a, size_t b) {
  assert(b > 0);
  return b * divceil(a, b);
}

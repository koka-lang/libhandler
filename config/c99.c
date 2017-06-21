#include <stdio.h>
int main()
{
#if (__STDC_VERSION__ >= 199901L)
  return 0;
#else
#ifndef __STDC_VERSION__
  fprintf(stderr,"no __STDC_VERSION__ defined\n");
#else
  fprintf(stderr,"detected __STD_VERSION__: %ld\n", 0__STDC_VERSION__ );
#endif
  return 1;
#endif
}
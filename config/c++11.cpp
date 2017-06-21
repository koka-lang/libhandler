#include <stdio.h>
int main()
{
#if (__cplusplus >= 201103L)
  return 0;
#else
#ifndef __cplusplus
  fprintf(stderr,"no __cplusplus defined\n");
#else
  fprintf(stderr,"detected __cplusplus: %ld\n", __cplusplus );
#endif
  return 1;
#endif
}
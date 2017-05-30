#include <setjmp.h>

int main()
{
  sigjmp_buf entry;
  if (sigsetjmp(entry, 0) != 0) {
    return 1; 
  }
  else {
    return 0;
  }
}
#include <setjmp.h>

int main()
{
  jmp_buf entry;
  if (setjmp(entry) != 0) {
    return 1; 
  }
  else {
    return 0;
  }
}
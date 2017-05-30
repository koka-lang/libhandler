#include <setjmp.h>

int main()
{
  jmp_buf entry;
  if (_setjmp(entry) != 0) {
    return 1; 
  }
  else {
    return 0;
  }
}
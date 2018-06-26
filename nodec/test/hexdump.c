#include "stdlib.h"
#include "stdio.h"
#include <ctype.h>

//---------------------------------------------------------------------------//
// get_print
//
// 	If the character is printable then return it otherwise return a period
//---------------------------------------------------------------------------//

static unsigned char get_print(unsigned char uc)
{
    if (isprint(uc))
        return uc;
    else
        return '.';
}

void hexDump(const void *addr, size_t len)
{
	int i;
	unsigned char buff[17];
	unsigned char *pc = (unsigned char*)addr;
	for (i = 0; i < len; i++) {
        const unsigned char uc = pc[i];
		if ((i % 16) == 0) {
			if (i != 0)
				printf("  %s\n", buff);
			printf("%p ", pc + i);
		}
		printf(" %02x", uc);
        buff[i % 16] = get_print(uc);
		buff[(i % 16) + 1] = '\0';
	}
	while ((i % 16) != 0) {
		printf("   ");
		i++;
	}
	printf("  %s\n", buff);
}
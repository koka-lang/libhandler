#include "stdlib.h"
#include "stdio.h"
#include <ctype.h>
#include "hexdump.h"

void hexDump(const void *addr, size_t len)
{
	int i;
	unsigned char buff[17];
	unsigned char *pc = (unsigned char*)addr;
	for (i = 0; i < len; i++) {
		if ((i % 16) == 0) {
			if (i != 0)
				printf("  %s\n", buff);
			printf("%p ", pc + i);
		}
		printf(" %02x", pc[i]);
		if (!isprint(pc[i]))
			buff[i % 16] = '.';
		else
			buff[i % 16] = pc[i];
		buff[(i % 16) + 1] = '\0';
	}
	while ((i % 16) != 0) {
		printf("   ");
		i++;
	}
	printf("  %s\n", buff);
}
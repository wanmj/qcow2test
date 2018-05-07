#include "fmtprint.h"
#include <stdio.h>

static unsigned int sum(unsigned char *a, unsigned int n) {
	unsigned int s = 0;
	for (unsigned int i = 0; i < n; ++i) {
		s += a[i];
	}
	return s;
}

void print(void *buffer, unsigned int size) {
	unsigned char *p = (unsigned char *)buffer;
	int flag = 1;
	for (unsigned int i = 0; i < size; ++i) {
		if (0 == i % 16) {
			//if (flag == 1) printf("0x%08X  ", i);
			if (sum(&p[i], 16) == 0) {
				if (flag == 1) {
					printf("0x%08X  ", i);
					printf("*\n");
				}
				//printf("\n");
				i += 15;
				flag = 0;
				continue;
			}
			else {
				printf("0x%08X  ", i);
				flag = 1;
			}
		}
		unsigned int temp = p[i];
		printf("%02X ", p[i]);
		if (0 == (i + 1) % 8) printf(" ");
		if (0 == (i + 1) % 16) printf("\n");
	}
}

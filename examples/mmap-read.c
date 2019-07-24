/*
 *
 * Copyright (c) 2003 The Regents of the University of California.  All
 * rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Neither the name of the University nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


/*
 * FUSD - The Framework for UserSpace Devices - Example program
 *
 * This mmap a file/device and change it a bit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static void hexdump(void *ptr_r, int size)
{
	int J, I;
	unsigned char *ptr = ptr_r;
	unsigned long Addr = 0;
	if (ptr == NULL) {
		puts("NULL POINTER");
		puts("-------------------------------------------------------------------------------------");
		return;
	}
	while (Addr <= size) {
		for (J = 0; J < 2; J++) {
			printf("%08p: ", Addr + ptr);
			for (I = 0; I < 16; I++, Addr++) { if (Addr <= size) { printf("%02lX ", (unsigned char) ptr[Addr]); } else { printf("   "); } }
			printf(" | "); Addr -= 16;
			for (I = 0; I < 16; I++, Addr++) { if (Addr <= size) { putchar(isprint(ptr[Addr]) ? ptr[Addr] : '.'); } else { putchar(' '); } }
			puts("");
		}
	}
	puts("-------------------------------------------------------------------------------------");
}

int main(int argc, char *argv[])
{
	int fd, i;
	char *ptr;
	int size = 0;
	struct stat FileStat;

	srand((unsigned int) time(NULL));

	if (argc != 3) {
		printf("Usage: %s file size");
	}

	fd = open(argv[1], O_RDWR);
	size = atoi(argv[2]);

	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	printf("ptr = %p\n", ptr);

	if ((ptr != NULL) && (ptr != MAP_FAILED)) {

		hexdump(ptr, size);

		/* Let's do some changes */
		for (i = 0; i < 128; i++) {
			ptr[i] ^= rand() % 0x100;
		}

		msync(ptr, size, MS_SYNC|MS_INVALIDATE);

		hexdump(ptr, size);
	}

	close(fd);

	return 0;
}

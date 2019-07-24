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
 * This example creates a a mmap-able buffer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mman.h>

#include "fusd.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define BUFFER_SIZE 1024

static char *mmap_buffer;

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

ssize_t mmaptest_read(struct fusd_file_info *file, char *user_buffer,
                      size_t user_length, loff_t *offset)
{
	int len;

	if (*offset > BUFFER_SIZE) {
		return 0;
	}

	len = MIN(user_length + (*offset), BUFFER_SIZE);
	memcpy(user_buffer, mmap_buffer + (*offset), len);
	*offset += len;
	return len;
}


int tester_mmap(struct fusd_file_info *file, int offset, size_t length, int prot, int flags,
                void **addr, size_t *out_length)
{

	printf("Got a mmap request from PID:%d [offset=%d, size=%d, prot=%X, flags=%X, addr=%p]\n",
		file->pid, offset, length, prot, flags, *addr);

	if (length <= BUFFER_SIZE) {

		*addr = mmap_buffer;
		*out_length = BUFFER_SIZE;
		return 0;
	}

	return -1;
}

int do_open(struct fusd_file_info *file)
{
	/* opens and closes always succeed */
	return 0;
}

int do_close(struct fusd_file_info *file)
{
	/* Show content of the buffer */
	hexdump(mmap_buffer, 512);
	return 0;
}


struct fusd_file_operations drums_fops = {
	open: do_open,
	read: mmaptest_read,
	mmap: tester_mmap,
	close: do_close
};

int main(int argc, char *argv[])
{
	int i;

	mmap_buffer = (char *)mmap(NULL, BUFFER_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

	if (fusd_register("mmap-tester", "mmaptest", "mmap-tester", 0666, NULL, &drums_fops) < 0) {
		fprintf(stderr, "mmap-tester register failed: %m\n");
		return -1;
	}

	memset(mmap_buffer, 0xAA, BUFFER_SIZE);

	fprintf(stderr, "calling fusd_run...\n");
	fusd_run();
	return 0;
}

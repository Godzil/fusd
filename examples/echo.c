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
 * Jeremy Elson <jelson@circlemud.org>
 *
 * echo.c: Example of how to use both the 'read' and 'write' callbacks.
 *
 * This example creates a single device, /dev/echo.  If you write
 * something to /dev/echo (e.g., "echo HI THERE > /dev/echo"), it gets
 * stored.  Then, when you read (e.g. "cat /dev/echo"), you get back
 * whatever you wrote most recently.
 *
 * $Id$ 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "fusd.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* EXAMPLE START echo.c */
char *data = NULL;
int data_length = 0;

ssize_t echo_read(struct fusd_file_info *file, char *user_buffer,
	      size_t user_length, loff_t *offset)
{
  /* if the user has read past the end of the data, return EOF */
  if (*offset >= data_length)
    return 0;

  /* only return as much data as we have */
  user_length = MIN(user_length, data_length - *offset);

  /* copy data to user starting from the first byte they haven't seen */
  memcpy(user_buffer, data + *offset, user_length);
  *offset += user_length;

  /* tell them how much data they got */
  return user_length;
}

ssize_t echo_write(struct fusd_file_info *file, const char *user_buffer,
		   size_t user_length, loff_t *offset)
{
  /* free the old data, if any */
  if (data != NULL) {
    free(data);
    data = NULL;
    data_length = 0;
  }

  /* allocate space for new data; return error if that fails */
  if ((data = malloc(user_length)) == NULL)
    return -ENOMEM;

  /* make a copy of user's data; tell the user we copied everything */
  memcpy(data, user_buffer, user_length);
  data_length = user_length;
  return user_length;
}
/* EXAMPLE STOP */

int do_open_or_close(struct fusd_file_info *file)
{
  return 0; /* opens and closes always succeed */
}


struct fusd_file_operations echo_fops = {
  open: do_open_or_close,
  read: echo_read,
  write: echo_write,
  close: do_open_or_close
};


int main(int argc, char *argv[])
{
  if (fusd_register("/dev/echo", "misc", "echo", 0666, NULL, &echo_fops) < 0) {
    perror("register of /dev/echo failed");
    exit(1);
  }

  fprintf(stderr, "calling fusd_run...\n");
  fusd_run();
  return 0;
}

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
 * console-read: This example demonstrates the easiest possible way to
 * block a client system call: by blocking the driver itself.  Not
 * recommended for anything but the most trivial drivers -- if you
 * need a template from which to start on a real driver, use pager.c
 * instead.
 *
 * $Id$
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "fusd.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))

int do_open_or_close(struct fusd_file_info *file)
{
  return 0; /* attempts to open and close this file always succeed */
}

/* EXAMPLE START console-read.c */
ssize_t do_read(struct fusd_file_info *file, char *user_buffer, 
	    size_t user_length, loff_t *offset)
{
  char buf[128];
  
  if (*offset > 0)
    return 0;

  /* print a prompt */
  printf("Got a read from pid %d.  What do we say?\n> ", file->pid);
  fflush(stdout);

  /* get a response from the console */
  if (fgets(buf, sizeof(buf) - 1, stdin) == NULL)
    return 0;

  /* compute length of the response, and return */
  user_length = MIN(user_length, strlen(buf));
  memcpy(user_buffer, buf, user_length);
  *offset += user_length;
  return user_length;
}
/* EXAMPLE STOP */

int main(int argc, char *argv[])
{
  struct fusd_file_operations fops = {
    open: do_open_or_close,
    read: do_read,
    close: do_open_or_close };
  
  if (fusd_register("/dev/console-read", "test", "console-read", 0666, NULL, &fops) < 0)
    perror("Unable to register device");
  else {
    printf("/dev/console-read should now exist - calling fusd_run...\n");
    fusd_run();
  }
  return 0;
}


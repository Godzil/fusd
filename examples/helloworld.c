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
 * hello-world: Simply creates a device called /dev/hello-world, which
 * greets you when you try to get it.
 *
 * $Id: helloworld.c,v 1.11 2003/07/11 22:29:38 cerpa Exp $
 */

/* EXAMPLE START helloworld.c */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "fusd.h"

#define GREETING "Hello, world!\n"

int do_open_or_close(struct fusd_file_info *file)
{
  return 0; /* attempts to open and close this file always succeed */
}

ssize_t do_read(struct fusd_file_info *file, char *user_buffer, 
	    size_t user_length, loff_t *offset)
{
  int retval = 0;
  
  /* The first read to the device returns a greeting.  The second read
   * returns EOF. */
  if (*offset == 0) {
    if (user_length < strlen(GREETING))
      retval = -EINVAL;  /* the user must supply a big enough buffer! */
    else {
      memcpy(user_buffer, GREETING, strlen(GREETING)); /* greet user */
      retval = strlen(GREETING); /* retval = number of bytes returned */
      *offset += retval; /* advance user's file pointer */
    }
  }
  
  return retval;
}

int main(int argc, char *argv[])
{
  struct fusd_file_operations fops = {
    open: do_open_or_close,
    read: do_read,
    close: do_open_or_close };
  
  if (fusd_register("/dev/hello-world", "test", "hello-world", 0666, NULL, &fops) < 0)
    perror("Unable to register device");
  else {
    printf("/dev/hello-world should now exist - calling fusd_run...\n");
    fusd_run();
  }
  return 0;
}
/* EXAMPLE STOP helloworld.c */

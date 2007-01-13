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
 * uid-filter.  This program shows how you can use some of the
 * meta-data provided in the fusd_file_info structure to affect your
 * driver's behavior.
 *
 * In particular, this driver creates a device, /dev/my-pid, that can
 * not be read by anyone other than the driver owner (not even root!).
 * When you read from the device, it returns your PID to you.
 *
 * $Id: uid-filter.c,v 1.4 2003/07/11 22:29:39 cerpa Exp $
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "fusd.h"


int do_close(struct fusd_file_info *file)
{
  return 0; /* attempts to close the file always succeed */
}

/* EXAMPLE START uid-filter.c */
int do_open(struct fusd_file_info *file)
{
  /* If the UID of the process trying to do the read doesn't match the
   * UID of the owner of the driver, return -EPERM.  If you run this
   * driver as a normal user, even root won't be able to read from the
   * device file created! */
  if (file->uid != getuid())
    return -EPERM;

  return 0;
}

int do_read(struct fusd_file_info *file, char *user_buffer,
	    size_t user_length, loff_t *offset)
{
  char buf[128];
  int len;

  /* The first read to the device returns a greeting.  The second read
   * returns EOF. */
  if (*offset != 0)
    return 0;

  /* len gets set to the number of characters written to buf */
  len = sprintf(buf, "Your PID is %d.  Have a nice day.\n", file->pid);

  /* NEVER return more data than the user asked for */
  if (user_length < len)
    len = user_length;

  memcpy(user_buffer, buf, len);
  *offset += len;
  return len;
}
/* EXAMPLE STOP uid-filter.c */


int main(int argc, char *argv[])
{
  struct fusd_file_operations fops = {
    open: do_open,
    read: do_read,
    close: do_close };
  
  if (fusd_register("/dev/my-pid", "misc", "my-pid", 0666, NULL, &fops) < 0)
    perror("Unable to register device");
  else {
    printf("/dev/my-pid should now exist - calling fusd_run...\n");
    fusd_run();
  }
  return 0;
}

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
 * drums.c: Example of how to pass data to a callback, inspired by
 * Alessandro Rubini's similar example in his article for Linux
 * Magazine (http://www.linux.it/kerneldocs/devfs/)
 *
 * This example creates a bunch of devices in the /dev/drums
 * directory: /dev/drums/bam, /dev/drums/bum, etc.  If you cat one of
 * these devices, it returns a string that's the same as its name.
 *
 * $Id: drums.c,v 1.4 2003/07/11 22:29:38 cerpa Exp $
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "fusd.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))


/* EXAMPLE START drums.c */
static char *drums_strings[] = {"bam", "bum", "beat", "boom",
				"bang", "crash", NULL};

int drums_read(struct fusd_file_info *file, char *user_buffer,
	       size_t user_length, loff_t *offset)
{
  int len;
  char sound[128];

  /* file->device_info is what we passed to fusd_register when we
   * registered the device */
  strcpy(sound, (char *) file->device_info);
  strcat(sound, "\n");

  /* 1st read returns the sound; 2nd returns EOF */
  if (*offset != 0)
    return 0;

  /* NEVER return more data than the user asked for */
  len = MIN(user_length, strlen(sound));
  memcpy(user_buffer, sound, len);
  *offset += len;
  return len;
}

/* EXAMPLE STOP drums.c */


int do_open_or_close(struct fusd_file_info *file)
{
  return 0; /* opens and closes always succeed */
}


struct fusd_file_operations drums_fops = {
  open: do_open_or_close,
  read: drums_read,
  close: do_open_or_close
};

/* EXAMPLE START drums.c */
int main(int argc, char *argv[])
{
  int i;
  char buf[128];
  char devname[128];

  for (i = 0; drums_strings[i] != NULL; i++) {
    sprintf(buf, "/dev/drums/%s", drums_strings[i]);
    sprintf(devname, "drum%s", drums_strings[i]);
    if (fusd_register(buf, "drums", devname, 0666, drums_strings[i], &drums_fops) < 0)
      fprintf(stderr, "%s register failed: %m\n", drums_strings[i]);
  }

  fprintf(stderr, "calling fusd_run...\n");
  fusd_run();
  return 0;
}
/* EXAMPLE STOP drums.c */

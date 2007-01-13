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
 * drums2.c: Another example of how to pass data to a callback,
 * inspired by Alessandro Rubini's similar example in his article for
 * Linux Magazine (http://www.linux.it/kerneldocs/devfs/)
 *
 * Like the original drums.c, this example creates a bunch of devices
 * in the /dev/drums directory: /dev/drums/bam, /dev/drums/bum, etc.
 * However, it also uses the private_data structure to keep per-file
 * state, and return a string unique to each user of the device.
 *
 * Note, unlike the original drums.c, this driver does not use *offset
 * to remember if this user has read before; cat /dev/drums/X will
 * read infinitely
 *
 * $Id$
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "fusd.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))


/* EXAMPLE START drums2.c */
struct drum_info {
  char *name;
  int num_users;
} drums[] = {
  { "bam", 0 },
  { "bum", 0 },
  /* ... */
/* EXAMPLE STOP */
  { "beat", 0 },
  { "boom", 0 },
  { "bang", 0 },
  { "crash", 0 },
/* EXAMPLE START drums2.c */
  { NULL, 0 }
};

int drums_open(struct fusd_file_info *file)
{
  /* file->device_info is what we passed to fusd_register when we
   * registered the device.  It's a pointer into the "drums" struct. */
  struct drum_info *d = (struct drum_info *) file->device_info;

  /* Store this user's unique user number in their private_data */
  file->private_data = (void *) ++(d->num_users);

  return 0; /* return success */
}

int drums_read(struct fusd_file_info *file, char *user_buffer,
	       size_t user_length, loff_t *offset)
{
  struct drum_info *d = (struct drum_info *) file->device_info;
  int len;
  char sound[128];

  sprintf(sound, "You are user %d to hear a drum go '%s'!\n",
	  (int) file->private_data, d->name);

  len = MIN(user_length, strlen(sound));
  memcpy(user_buffer, sound, len);
  return len;
}
/* EXAMPLE STOP */


int drums_close(struct fusd_file_info *file)
{
  return 0; /* closes always succeed */
}


struct fusd_file_operations drums_fops = {
  open: drums_open,
  read: drums_read,
  close: drums_close
};

/* EXAMPLE START drums2.c */

int main(int argc, char *argv[])
{
  struct drum_info *d;
  char buf[128];
  char devname[128];

  for (d = drums; d->name != NULL; d++) {
    sprintf(buf, "/dev/drums/%s", d->name);
    sprintf(devname, "drum%s", d->name);
    if (fusd_register(buf, "drums", devname, 0666, d, &drums_fops) < 0)
      fprintf(stderr, "%s register failed: %m\n", d->name);
  }
  /* ... */
/* EXAMPLE STOP */

  fprintf(stderr, "calling fusd_run...\n");
  fusd_run();
  return 0;
}




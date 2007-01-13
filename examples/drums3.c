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
 * drums3.c: This example shows how to wait for both FUSD and non-FUSD
 * events at the same time.  Instead of using fusd_run, we keep
 * control of main() by using our own select loop.
 *
 * Like the original drums.c, this example creates a bunch of devices
 * in the /dev/drums directory: /dev/drums/bam, /dev/drums/bum, etc.
 * However, it also prints a prompt to the console, asking the user if
 * how loud the drums should be.
 *
 * $Id: drums3.c,v 1.3 2003/07/11 22:29:38 cerpa Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "fusd.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))


static char *drums_strings[] = {"bam", "bum", "beat", "boom",
				"bang", "crash", NULL};

int volume = 2; /* default volume is 2 */

int drums_read(struct fusd_file_info *file, char *user_buffer,
	       size_t user_length, loff_t *offset)
{
  int len;
  char sound[128], *c;

  /* 1st read returns the sound; 2nd returns EOF */
  if (*offset != 0)
    return 0;

  if (volume == 1)
    strcpy(sound, "(you hear nothing)");
  else {
    strcpy(sound, (char *) file->device_info);

    if (volume >= 3)
      for (c = sound; *c; c++)
	*c = toupper(*c);

    if (volume >= 4)
      strcat(sound, "!!!!!");
  }

  strcat(sound, "\n");

  /* NEVER return more data than the user asked for */
  len = MIN(user_length, strlen(sound));
  memcpy(user_buffer, sound, len);
  *offset += len;
  return len;
}


int do_open_or_close(struct fusd_file_info *file)
{
  return 0; /* opens and closes always succeed */
}


struct fusd_file_operations drums_fops = {
  open: do_open_or_close,
  read: drums_read,
  close: do_open_or_close
};


void read_volume(int fd)
{
  char buf[100];
  int new_vol = 0, retval;

  if (fd < 0)
    goto prompt;

  retval = read(fd, buf, sizeof(buf)-1);

  if (retval >= 0) {
    buf[retval] = '\0';

    if (*buf == 'q') {
      printf("Goodbye...\n");
      exit(0);
    }

    new_vol = atoi(buf);
  }

  if (new_vol >= 1 && new_vol <= 4) {
    volume = new_vol;
    printf("Volume changed to %d\n", volume);
  } else {
    printf("Invalid volume!\n");
  }

 prompt:
  printf("\nHow loud would you like the /dev/drums?\n");
  printf("   1 - Inaudible\n");
  printf("   2 - Quiet\n");
  printf("   3 - Loud\n");
  printf("   4 - Permanent ear damage!\n");
  printf("   q - Exit program\n");
  printf("Your choice? ");
  fflush(stdout);
}


  

int main(int argc, char *argv[])
{
  int i;
  char buf[128];
  char devname[128];
  fd_set fds, tmp;
  int max;

  for (i = 0; drums_strings[i] != NULL; i++) {
    sprintf(buf, "/dev/drums/%s", drums_strings[i]);
    sprintf(devname, "drum%s", drums_strings[i]);
    if (fusd_register(buf, "drums", devname, 0666, drums_strings[i], &drums_fops) < 0)
      fprintf(stderr, "%s register failed: %m\n", drums_strings[i]);
  }

  /* print the initial prompt to the user */
  read_volume(-1);

/* EXAMPLE START drums3.c */
  /* initialize the set */
  FD_ZERO(&fds);

  /* add stdin to the set */
  FD_SET(STDIN_FILENO, &fds);
  max = STDIN_FILENO;

  /* add all FUSD fds to the set */
  fusd_fdset_add(&fds, &max);

  while (1) {
    tmp = fds;
    if (select(max+1, &tmp, NULL, NULL, NULL) < 0)
      perror("selecting");
    else {
      /* if stdin is readable, read the user's response */
      if (FD_ISSET(STDIN_FILENO, &tmp))
	read_volume(STDIN_FILENO);

      /* call any FUSD callbacks that have messages waiting */
      fusd_dispatch_fdset(&tmp);
    }
  }
/* EXAMPLE STOP drums3.c */
}




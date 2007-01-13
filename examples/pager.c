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
 * pagerd: simple daemon to accept page signals from underlying page
 * devices, and redistribute those pages to applications.
 *
 * The application itself is not especially useful, but this example
 * program has proved very valuable as a generic template for FUSD
 * drivers that service multiple clients, and implement both blocking
 * and selectable devices.  This file is a good place to start for
 * writing drivers.  See logring.c for a more complex real-world
 * application based on this template.
 *
 * How to use the pager:
 *
 * Interface for devices that generate pages: write "page" to
 * /dev/pager/input
 *
 * Interface for programs waiting for pages: read from (or, select on
 * and then read from) /dev/pager/notify.  reads will unblock when a
 * page arrives.  Note that if more than one page arrives before you
 * read, you'll only get the most recent one.  In other words, you are
 * guaranteed to get at least one page. 
 *
 * Important: in order to guarantee that you do not miss any pages,
 * you MUST NOT close the file descriptor in between reads/selects.
 * If you close the FD and then reopen it, there will be a race (pages
 * that arrive between the close and open will not be delivered).
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <fusd.h>


/* EXAMPLE START pager-open.c */
/* per-client structure to keep track of who has an open FD to us */
struct pager_client {
  int last_page_seen;   /* seq. no. of last page this client has seen */
  struct fusd_file_info *read; /* outstanding read request, if any */
  struct fusd_file_info *polldiff; /* outstanding polldiff request */ 
  struct pager_client *next;   /* to construct the linked list */
};

struct pager_client *client_list = NULL; /* list of clients (open FDs) */
int last_page = 0; /* seq. no. of the most recent page to arrive */

/* EXAMPLE STOP pager-open.c */

void pager_notify_complete_read(struct pager_client *c);
void pager_notify_complete_polldiff(struct pager_client *c);


/************************************************************************/

/* 
 * this function removes an element from a linked list.  the
 * pointer-manipulation insanity below is a trick that prevents the
 * "element to be removed is the head of the list" from being a
 * special case.
 */
void client_list_remove(struct pager_client *c)
{
  struct pager_client **ptr;

  if (c == NULL || client_list == NULL)
    return;

  for (ptr = &client_list; *ptr != c; ptr = &((**ptr).next)) {
    if (!*ptr) {
      fprintf(stderr, "trying to remove a client that isn't in the list\n");
      return;
    }
  }
  *ptr = c->next;
}


/* EXAMPLE START pager-open.c */
/* open on /dev/pager/notify: create state for this client */
static int pager_notify_open(struct fusd_file_info *file)
{
  /* create state for this client */
  struct pager_client *c = malloc(sizeof(struct pager_client));

  if (c == NULL)
    return -ENOBUFS;

  /* initialize fields of this client state */
  memset(c, 0, sizeof(struct pager_client));
  c->last_page_seen = last_page;

  /* save the pointer to this state so it gets returned to us later */
  file->private_data = c;

  /* add this client to the client list */
  c->next = client_list;
  client_list = c;
  
  return 0;
}
/* EXAMPLE STOP pager-open.c */


/* EXAMPLE START pager-close.c */
/* close on /dev/pager/notify: destroy state for this client */
static int pager_notify_close(struct fusd_file_info *file)
{
  struct pager_client *c;

  if ((c = (struct pager_client *) file->private_data) != NULL) {

    /* take this client off our client list */
    client_list_remove(c);

    /* if there is a read outstanding, free the state */
    if (c->read != NULL) {
      fusd_destroy(c->read);
      c->read = NULL;
    }
    /* destroy any outstanding polldiffs */
    if (c->polldiff != NULL) {
      fusd_destroy(c->polldiff);
      c->polldiff = NULL;
    }

    /* get rid of the struct */
    free(c);
    file->private_data = NULL;
  }
  return 0;
}
/* EXAMPLE STOP pager-close.c */


/*
 * read on /dev/pager/notify: store the fusd_file_info pointer.  then call
 * complete_read, which will immediately call fusd_return, if there is
 * a page already waiting.
 *
 * Note that this shows a trick we use commonly in FUSD drivers: you
 * are allowed to call fusd_return() from within a callback as long as
 * you return -FUSD_NOREPLY.  In other words, a driver can EITHER
 * return a real return value from its callback, OR call fusd_return
 * explicitly, but not both.
 */
/* EXAMPLE START pager-read.c */
ssize_t pager_notify_read(struct fusd_file_info *file, char *buffer,
			  size_t len, loff_t *offset)
{
  struct pager_client *c = (struct pager_client *) file->private_data;

  if (c == NULL || c->read != NULL) {
    fprintf(stderr, "pager_read's arguments are confusd, alas");
    return -EINVAL;
  }

  c->read = file;
  pager_notify_complete_read(c);
  return -FUSD_NOREPLY;
}

/* EXAMPLE STOP pager-read.c */

/*
 * This function "completes" a read: that is, matches up a client who
 * is requesting data with data that's waiting to be served.
 *
 * This function is called in two cases:
 *
 *   1- When a new read request comes in.  The driver might be able to
 *   complete immediately, if a page arrived between the time the
 *   process opened the device and performed the read.  This is the
 *   common case for clients that use select.  hasn't seen yet - this
 *   is normal if )
 *
 *   2- When a new page arrives, all readers are unblocked
 */
/* EXAMPLE START pager-read.c */
void pager_notify_complete_read(struct pager_client *c)
{
  /* if there is no outstanding read, do nothing */
  if (c == NULL || c->read == NULL)
    return;

  /* if there are no outstanding pages, do nothing */
  if (c->last_page_seen >= last_page)
    return;

  /* bring this client up to date with the most recent page */
  c->last_page_seen = last_page;

  /* and notify the client by unblocking the read (read returns 0) */
  fusd_return(c->read, 0);
  c->read = NULL;
}
/* EXAMPLE STOP pager-read.c */


/* This function is only called on behalf of clients who are trying to
 * use select().  The kernel keeps us up to date on what it thinks the
 * current "poll state" is, i.e. readable and/or writable.  The kernel
 * calls this function every time its assumption about the current
 * poll state changes.  Every time the driver's notion of the state
 * differs from what the kernel's cached value, it should return the
 * poll_diff request with the updated state.  Note that a 2nd request
 * may come from the kernel before the driver has returned the first
 * one; if this happens, use fusd_destroy() to get rid of the older one.
 */
/* EXAMPLE START pager-polldiff.c */
ssize_t pager_notify_polldiff(struct fusd_file_info *file,
			      unsigned int cached_state)
{
  struct pager_client *c = (struct pager_client *) file->private_data;

  if (c == NULL)
    return -EINVAL;

  /* if we're already holding a polldiff request that we haven't
   * replied to yet, destroy the old one and hold onto only the new
   * one */
  if (c->polldiff != NULL) {
    fusd_destroy(c->polldiff);
    c->polldiff = NULL;
  }

  c->polldiff = file;
  pager_notify_complete_polldiff(c);
  return -FUSD_NOREPLY;
}

/* EXAMPLE STOP pager-polldiff.c */


/*
 * complete_polldiff: if a client has an outstanding 'polldiff'
 * request, possibly return updated poll-state information to the
 * kernel, if indeed the state has changed.
 */
/* EXAMPLE START pager-polldiff.c */
void pager_notify_complete_polldiff(struct pager_client *c)
{
  int curr_state, cached_state;

  /* if there is no outstanding polldiff, do nothing */
  if (c == NULL || c->polldiff == NULL)
    return;

  /* figure out the "current" state: i.e. whether or not the pager
   * is readable for this client based on the last page it saw */
  if (c->last_page_seen < last_page)
    curr_state = FUSD_NOTIFY_INPUT; /* readable */
  else
    curr_state = 0; /* not readable or writable */

  /* cached_state is what the kernel *thinks* the state is */
  cached_state = fusd_get_poll_diff_cached_state(c->polldiff);

  /* if the state is not what the kernel thinks it is, notify the
     kernel of the change */
  if (curr_state != cached_state) {
    fusd_return(c->polldiff, curr_state);
    c->polldiff = NULL;
  }
}
/* EXAMPLE STOP pager-polldiff.c */



/*
 * this handles a write on /dev/pager/input.  this is called by one of
 * the underlying page devices when a page arrives.  if a device
 * writes "page" to this interface, a page is queued for everyone
 * using the notify interface.
 */
#define CASE(x) if ((found == 0) && !strcmp(tmp, x) && (found = 1))
/* EXAMPLE START pager-read.c */

ssize_t pager_input_write(struct fusd_file_info *file,
			  const char *buffer, size_t len, loff_t *offset)
{
  struct pager_client *c;

  /* ... */
  /* EXAMPLE STOP pager-read.c */
  char tmp[1024];
  int found = 0;

  if (len > sizeof(tmp) - 1)
    len = sizeof(tmp) - 1;
  
  strncpy(tmp, buffer, len);
  tmp[len] = '\0';

  /* strip trailing \n's */
  while (tmp[len-1] == '\n')
    tmp[--len] = '\0';

  /* EXAMPLE START pager-read.c */

  CASE("page") {
    last_page++;

    for (c = client_list; c != NULL; c = c->next) {
      pager_notify_complete_polldiff(c);
      pager_notify_complete_read(c);
    }
  }
  /* EXAMPLE STOP pager-read.c */

  /* other commands (if there ever are any) can go here */

  if (!found)
    return -EINVAL;
  else
    return len;
}
#undef CASE


static int fusd_success(struct fusd_file_info *file)
{
  return 0;
}


int main(int argc, char *argv[])
{
  /* register the input device */
  fusd_simple_register("/dev/pager/input", "pager", "input", 0666, NULL,
                       open: fusd_success, close: fusd_success,
                       write: pager_input_write);

  /* register the notification device */
  fusd_simple_register("/dev/pager/notify", "pager", "notify", 0666, NULL,
                       open: pager_notify_open,
		       close: pager_notify_close,
                       read: pager_notify_read,
  		       poll_diff: pager_notify_polldiff);

  printf("calling fusd_run; reads from /dev/pager/notify will now block\n"
	 "until someone writes 'page' to /dev/pager/input...\n");
  fusd_run();

  return 0;
}


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
 * logring.c: Implementation of a circular buffer log device
 *
 * logring makes it easy to access the most recent (and only the most
 * recent) output from a process. It works just like "tail -f" on a
 * log file, except that the storage required never grows. This can be
 * useful in embedded systems where there isn't enough memory or disk
 * space for keeping complete log files, but the most recent debugging
 * messages are sometimes needed (e.g., after an error is observed).
 *
 * Logring uses FUSD to implement a character device, /dev/logring,
 * that acts like a named pipe that has a finite, circular buffer.
 * The size of the buffer is given as a command-line argument.  As
 * more data is written into the buffer, the oldest data is discarded.
 * A process that reads from the logring device will first read the
 * existing buffer, then block and see new data as it's written,
 * similar to monitoring a log file using "tail -f".
 *
 * Non-blocking reads are supported; if a process needs to get the
 * current contents of the log without blocking to wait for new data,
 * it can set the O_NONBLOCK flag when it does the open(), or set it
 * later using ioctl().
 *
 * The select() interface is also supported; programs can select on
 * /dev/logring to be notified when new data is available.
 *
 * Run this example program by typing "logring X", where X is the size
 * of the circular buffer in bytes.  Then, type "cat /dev/logring" in
 * one shell.  The cat process will block, waiting for data, similar
 * to "tail -f".  From another shell, write to the logring (e.g.,
 * "echo Hi there > /dev/logring".)  The 'cat' process will see the
 * message appear.
 *
 * Note: this example program is based on "emlog", a true Linux kernel
 * module with identical functionality.  If you find logring useful,
 * but want to use it on a system that does not have FUSD, check out
 * emlog at http://www.circlemud.org/~jelson/software/emlog.
 *
 * $Id: logring.c,v 1.8 2003/07/11 22:29:39 cerpa Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <fusd.h>


/* per-client structure to keep track of who has an open FD to us */
struct logring_client {

  /* used to store outstanding read and polldiff requests */
  struct fusd_file_info *read;
  struct fusd_file_info *polldiff;

  /* to construct the linked list */
  struct logring_client *next;
};

/* list of currently open file descriptors */
struct logring_client *client_list = NULL;

char *logring_data = NULL;	/* the data buffer used for the logring */
int logring_size = 0;		/* buffer space in the logring */
int logring_writeindex = 0;	/* write point in the logring array */
int logring_readindex = 0;	/* read point in the logring array */
int logring_offset = 0;		/* how far into the total stream is
				 * logring_read pointing? */

/* amount of data in the queue */
#define LOGRING_QLEN  (logring_writeindex >= logring_readindex ? \
         logring_writeindex - logring_readindex : \
         logring_size - logring_readindex + logring_writeindex)

/* stream byte number of the last byte in the queue */
#define LOGRING_FIRST_EMPTY_BYTE (logring_offset + LOGRING_QLEN)

#define MIN(x, y) ((x) < (y) ? (x) : (y))

/************************************************************************/

/* 
 * this function removes an element from a linked list.  the
 * pointer-manipulation insanity below is a trick that prevents the
 * "element to be removed is the head of the list" from being a
 * special case.
 */
void client_list_remove(struct logring_client *c)
{
  struct logring_client **ptr;

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


/* open on /dev/logring: create state for this client */
static int logring_open(struct fusd_file_info *file)
{
  /* create state for this client */
  struct logring_client *c = malloc(sizeof(struct logring_client));

  if (c == NULL)
    return -ENOBUFS;

  /* initialize fields of this client state */
  memset(c, 0, sizeof(struct logring_client));

  /* save the pointer to this state so it gets returned to us later */
  file->private_data = c;

  /* add this client to the client list */
  c->next = client_list;
  client_list = c;
  
  return 0;
}


/* close on /dev/logring: destroy state for this client */
static int logring_close(struct fusd_file_info *file)
{
  struct logring_client *c;

  if ((c = (struct logring_client *) file->private_data) != NULL) {

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



/*
 * This function "completes" a read: that is, matches up a client who
 * is requesting data with data that's waiting to be served.
 *
 * This function is called in two cases:
 *
 *   1- When a new read request comes in (it might be able to complete
 *   immediately, if there's data waiting that the client hasn't seen
 *   yet)
 *
 *   2- When new data comes in (the new data might be able to complete
 *   a read that had been previously blocked)
 */
void logring_complete_read(struct logring_client *c)
{
  loff_t *user_offset;
  char *user_buffer;
  size_t user_length;
  int bytes_copied = 0, n, start_point, retval;


  /* if there is no outstanding read, do nothing */
  if (c == NULL || c->read == NULL)
    return;

  /* retrieve the read callback's arguments */
  user_offset = fusd_get_offset(c->read);
  user_buffer = fusd_get_read_buffer(c->read);
  user_length = fusd_get_length(c->read);

  /* is the client trying to read data that has scrolled off? */
  if (*user_offset < logring_offset)
    *user_offset = logring_offset;

  /* is there new data this user hasn't seen yet, or are we at EOF? */
  /* If we have reached EOF:
   *     If this is a nonblocking read, return EAGAIN.  
   *     else return without doing anything; keep the read blocked.
   */
  if (*user_offset >= LOGRING_FIRST_EMPTY_BYTE) {
    if (c->read->flags & O_NONBLOCK) {
      retval = -EAGAIN;
      goto done;
    } else {
      return;
    }
  }

  /* find the smaller of the total bytes we have available and what
   * the user is asking for */
  user_length = MIN(user_length, LOGRING_FIRST_EMPTY_BYTE - *user_offset);
  retval = user_length;

   /* figure out where to start copying data from, based on user's offset */
  start_point =
    (logring_readindex + (*user_offset-logring_offset)) % logring_size;

  /* copy the (possibly noncontiguous) data into user's buffer) */
  while (user_length) {
    n = MIN(user_length, logring_size - start_point);
    memcpy(user_buffer + bytes_copied, logring_data + start_point, n);
    bytes_copied += n;
    user_length -= n;
    start_point = (start_point + n) % logring_size;
  }

  /* advance the user's file pointer */
  *user_offset += retval;

 done:
  /* and complete the read system call */
  fusd_return(c->read, retval);
  c->read = NULL;
}



/*
 * read on /dev/logring: store the fusd_file_info pointer.  then call
 * complete_read, which will immediately call fusd_return, if there is
 * data already waiting.
 *
 * Note that this shows a trick we use commonly in FUSD drivers: you
 * are allowed to call fusd_return() from within a callback as long as
 * you return -FUSD_NOREPLY.  In other words, a driver can EITHER
 * return a real return value from its callback, OR call fusd_return
 * explicitly, but not both.
 */
static ssize_t logring_read(struct fusd_file_info *file, char *buffer,
			    size_t len, loff_t *offset)
{
  struct logring_client *c = (struct logring_client *) file->private_data;

  if (c == NULL || c->read != NULL) {
    fprintf(stderr, "logring_read's arguments are confusd, alas");
    return -EINVAL;
  }

  c->read = file;
  logring_complete_read(c);
  return -FUSD_NOREPLY;
}


/*
 * complete_polldiff: if a client has an outstanding 'polldiff'
 * request, possibly return updated poll-state information to the
 * kernel, if indeed the state has changed.
 */
void logring_complete_polldiff(struct logring_client *c)

{
  int curr_state, cached_state;

  /* if there is no outstanding polldiff, do nothing */
  if (c == NULL || c->polldiff == NULL)
    return;

  /* figure out the "current" state: i.e. whether or not the logring
   * is readable for this client based on its current position in the
   * stream.  The logring is *always* writable. */
  if (*(fusd_get_offset(c->polldiff)) < LOGRING_FIRST_EMPTY_BYTE)
    curr_state = FUSD_NOTIFY_INPUT | FUSD_NOTIFY_OUTPUT; /* read and write */
  else
    curr_state = FUSD_NOTIFY_OUTPUT; /* writable only */

  /* cached_state is what the kernel *thinks* the state is */
  cached_state = fusd_get_poll_diff_cached_state(c->polldiff);

  /* if the state is not what the kernel thinks it is, notify the
     kernel of the change */
  if (curr_state != cached_state) {
    fusd_return(c->polldiff, curr_state);
    c->polldiff = NULL;
  }
}


/* This function is only called on behalf of clients who are trying to
 * use select().  The kernel keeps us up to date on what it thinks the
 * current "poll state" is, i.e. readable and/or writable.  The kernel
 * calls this function every time its assumption about the current
 * poll state changes.  Every time the driver's notion of the state
 * differs from what the kernel thinks it is, it should return the
 * poll_diff request with the updated state.  Note that a 2nd request
 * may come from the kernel before the driver has returned the first
 * one; if this happens, use fusd_destroy() to get rid of the older one.
 */
ssize_t logring_polldiff(struct fusd_file_info *file, unsigned int flags)
{
  struct logring_client *c = (struct logring_client *) file->private_data;

  if (c == NULL)
    return -EIO;

  /* if we're already holding a polldiff request that we haven't
   * replied to yet, destroy the old one and hold onto only the new
   * one */
  if (c->polldiff != NULL) {
    fusd_destroy(c->polldiff);
    c->polldiff = NULL;
  }

  c->polldiff = file;
  logring_complete_polldiff(c);
  return -FUSD_NOREPLY;
}


/*
 * a write on /dev/logring: first, copy the data from the user into our
 * data queue.  Then, complete any reads and polldiffs that might be
 * outstanding.
 */
ssize_t logring_write(struct fusd_file_info *file, const char *buffer,
		      size_t len, loff_t *offset)
{
  struct logring_client *c;
  int overflow = 0, bytes_copied = 0, n, retval;

  /* if the message is longer than the buffer, just take the beginning
   * of it, in hopes that the reader (if any) will have time to read
   * before we wrap around and obliterate it */
  len = MIN(len, logring_size - 1);
  retval = len;

  if (len + LOGRING_QLEN >= (logring_size-1)) {
    overflow = 1;

    /* in case of overflow, figure out where the new buffer will
     * begin.  we start by figuring out where the current buffer ENDS:
     * logring_offset + LOGRING_QLEN.  we then advance the end-offset
     * by the length of the current write, and work backwards to
     * figure out what the oldest unoverwritten data will be (i.e.,
     * size of the buffer).  was that all quite clear? :-) */
    logring_offset = logring_offset + LOGRING_QLEN + len - logring_size + 1;
  }
    
  while (len) {
    /* how many contiguous bytes are available from the write point to
     * the end of the circular buffer? */
    n = MIN(len, logring_size - logring_writeindex);
    memcpy(logring_data + logring_writeindex, buffer + bytes_copied, n);
    bytes_copied += n;
    len -= n;
    logring_writeindex = (logring_writeindex + n) % logring_size;
  }

  /* if there was an overflow (i.e., new data wrapped around and
   * overwrote old data that had not yet been read), then, reset the
   * read point to be whatever the oldest data is that we have. */
  if (overflow)
    logring_readindex = (logring_writeindex + 1) % logring_size;

  /* now, complete any blocked reads and/or polldiffs */
  for (c = client_list; c != NULL; c = c->next) {
    logring_complete_read(c);
    logring_complete_polldiff(c);
  }

  /* now tell the client how many bytes we acutally wrote */
  return retval;
}


int main(int argc, char *argv[])
{
  char *name;

  /* size must be provided, and an optional logring name */
  if (argc != 2 && argc != 3) {
    fprintf(stderr, "usage: %s <logring-size> [logring-name]\n", argv[0]);
    exit(1);
  }

  name = (argc == 3 ? argv[2] : "/dev/logring");

  /* convert the arg to an int and alloc memory for the logring */
  if ((logring_size = atoi(argv[1])) <= 0) {
    fprintf(stderr, "invalid logring size; it must be >0\n");
    exit(1);
  }

  if ((logring_data = (char *) malloc(sizeof(char) * logring_size)) == NULL) {
    fprintf(stderr, "couldn't allocate %d bytes!\n", logring_size);
    exit(1);
  }

  /* register the fusd device */
  fusd_simple_register(name, "misc", "logring", 0666, NULL,
                       open: logring_open, close: logring_close,
                       read: logring_read, write: logring_write,
		       poll_diff: logring_polldiff);

  printf("calling fusd_run; reads from /dev/logring will now block\n"
	 "until someone writes to /dev/logring...\n");
  fusd_run();

  return 0;
}


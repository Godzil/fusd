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
 * fusd userspace library: functions that know how to properly talk
 * to the fusd kernel module
 *
 * authors: jelson and girod
 *
 * $Id: libfusd.c,v 1.61 2003/07/11 22:29:39 cerpa Exp $
 */

char libfusd_c_id[] = "$Id: libfusd.c,v 1.61 2003/07/11 22:29:39 cerpa Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "fusd.h"
#include "fusd_msg.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* maximum number of messages processed by a single call to fusd_dispatch */
#define MAX_MESSAGES_PER_DISPATCH 40

/* used for fusd_run */
static fd_set fusd_fds;

/* default prefix of devices (often "/dev/") */
char *dev_root = NULL;

/* 
 * fusd_fops_set is an array that keeps track of the file operations
 * struct for each fusd fd.
 */
static fusd_file_operations_t fusd_fops_set[FD_SETSIZE];
fusd_file_operations_t null_fops = { NULL };

/*
 * accessor macros
 */
#define FUSD_GET_FOPS(fd) \
  (fusd_fops_set + (fd))

#define FUSD_SET_FOPS(fd,fi) \
  (fusd_fops_set[(fd)]=(*fi))

#define FUSD_FD_VALID(fd) \
  (((fd)>=0) && \
   ((fd)<FD_SETSIZE) && \
   (memcmp(FUSD_GET_FOPS(fd), &null_fops, sizeof(fusd_file_operations_t))))


/*
 * fusd_init
 * 
 * this is called automatically before the first
 * register call 
 */
void fusd_init()
{
  static int fusd_init_needed = 1;

  if (fusd_init_needed) {
    int i;

    fusd_init_needed = 0;

    for (i = 0; i < FD_SETSIZE; i++)
      FUSD_SET_FOPS(i, &null_fops);
    FD_ZERO(&fusd_fds);

    dev_root = DEFAULT_DEV_ROOT;
  }
}


int fusd_register(const char *name, const char* clazz, const char* devname, mode_t mode, void *device_info,
		  struct fusd_file_operations *fops)
{
  int fd = -1, retval = 0;
  fusd_msg_t message;

  /* need initialization? */
  fusd_init();

  /* make sure the name is valid and we have a valid set of fops... */
  if (name == NULL || fops == NULL) {
    fprintf(stderr, "fusd_register: invalid name or fops argument\n");
    retval = -EINVAL;
    goto done;
  }

  /*
   * convenience: if the first characters of the name you're trying
   * to register are SKIP_PREFIX (usually "/dev/"), skip over them.
   */
  if (dev_root != NULL && strlen(name) > strlen(dev_root) &&
      !strncmp(name, dev_root, strlen(dev_root))) {
    name += strlen(dev_root);
  }

  if (strlen(name) > FUSD_MAX_NAME_LENGTH) {
    fprintf(stderr, "name '%s' too long, sorry :(", name);
    retval = -EINVAL;
    goto done;
  }

  /* open the fusd control channel */
  if ((fd = open(FUSD_CONTROL_DEVNAME, O_RDWR | O_NONBLOCK)) < 0) {

    /* if the problem is that /dev/fusd does not exist, return the
     * message "Package not installed", which is hopefully more
     * illuminating than "no such file or directory" */
    if (errno == ENOENT) {
      fprintf(stderr, "libfusd: %s does not exist; ensure FUSD's kernel module is installed\n",
	      FUSD_CONTROL_DEVNAME);
      retval = -ENOPKG;
    } else {
      perror("libfusd: trying to open FUSD control channel");
      retval = -errno;
    }
    goto done;
  }

  /* fd in use? */
  if (FUSD_FD_VALID(fd)) {
    retval = -EBADF;
    goto done;
  }

  /* set up the message */
  memset(&message, 0, sizeof(message));
  message.magic = FUSD_MSG_MAGIC;
  message.cmd = FUSD_REGISTER_DEVICE;
  message.datalen = 0;
  strcpy(message.parm.register_msg.name, name);
  strcpy(message.parm.register_msg.clazz, clazz);
  strcpy(message.parm.register_msg.devname, devname);
  message.parm.register_msg.mode = mode;
  message.parm.register_msg.device_info = device_info;

  /* make the request */
  if (write(fd, &message, sizeof(fusd_msg_t)) < 0) {
    retval = -errno;
    goto done;
  }

  /* OK, store the new file state */
  FUSD_SET_FOPS(fd, fops);
  FD_SET(fd, &fusd_fds);

  /* success! */
 done:
  if (retval < 0) {
    if (fd >= 0)
      close(fd);
    errno = -retval;
    retval = -1;
  } else {
    errno = 0;
    retval = fd;
  }

  return retval;
}


int fusd_unregister(int fd)
{
  if (FUSD_FD_VALID(fd)) {
    /* clear fd location */
    FUSD_SET_FOPS(fd, &null_fops);
    FD_CLR(fd, &fusd_fds);
    /* close */
    return close(fd);
  }
  
  else {
    errno = EBADF;
    return -1;
  }
}


/* 
 * fusd_run: a convenience function for automatically running a FUSD
 * driver, for drivers that don't want to manually select on file
 * descriptors and call fusd_dispatch.  This function will
 * automatically select on all devices the user has registered and
 * call fusd_dispatch on any one that becomes readable.
 */
void fusd_run(void)
{
  fd_set tfds;
  int status;
  int maxfd;
  int i;

  /* locate maxmimum fd in use */
  for (maxfd=0, i=0; i < FD_SETSIZE; i++) {
    if (FD_ISSET(i, &fusd_fds)) {
      maxfd = i;
    }
  }
  maxfd++;


  while (1) {
    /* select */
    memmove(&tfds, &fusd_fds, sizeof(fd_set));
    status = select(maxfd, &tfds, NULL, NULL, NULL);

    /* error? */
    if (status < 0) {
      perror("libfusd: fusd_run: error on select");
      continue;
    }

    /* readable? */
    for (i = 0; i < maxfd; i++)
      if (FD_ISSET(i, &tfds))
	fusd_dispatch(i);
  }
}


/************************************************************************/


/* reads a fusd kernel-to-userspace message from fd, and puts a
 * fusd_msg into the memory pointed to by msg (we assume we are passed
 * a buffer managed by the caller).  if there is a data portion to the
 * message (msg->datalen > 0), we allocate memory for it, set data to
 * point to that memory.  the returned data pointer must also be
 * managed by the caller. */
static int fusd_get_message(int fd, fusd_msg_t *msg)
{
  /* read the header part into the kernel */
  if (read(fd, msg, sizeof(fusd_msg_t)) < 0) {
    if (errno != EAGAIN)
      perror("error talking to FUSD control channel on header read");
    return -errno;
  }
  msg->data = NULL; /* pointers in kernelspace have no meaning */

  if (msg->magic != FUSD_MSG_MAGIC) {
    fprintf(stderr, "libfusd magic number failure\n");
    return -EINVAL;
  }

  /* if there's a data part to the message, read it from the kernel. */
  if (msg->datalen) {
    if ((msg->data = malloc(msg->datalen + 1)) == NULL) {
      fprintf(stderr, "libfusd: can't allocate memory\n");
      return -ENOMEM;  /* this is bad, we are now unsynced */
    }

    if (read(fd, msg->data, msg->datalen) < 0) {
      perror("error talking to FUSD control channel on data read");
      free(msg->data);
      msg->data = NULL;
      return -EIO;
    }

    /* For convenience, we now ensure that the byte *after* the buffer
     * is set to 0.  (Note we malloc'd one extra byte above.) */
    msg->data[msg->datalen] = '\0';
  }

  return 0;
}


/*
 * fusd_fdset_add: given an FDSET and "max", add the currently valid
 * FUSD fds to the set and update max accordingly.
 */
void fusd_fdset_add(fd_set *set, int *max)
{
  int i;

  for (i = 0; i < FD_SETSIZE; i++) {
    if (FD_ISSET(i, &fusd_fds)) {
      FD_SET(i, set);
      if (i > *max) {
	*max = i;
      }
    }
  }
}



/*
 * fusd_dispatch_fdset: given an fd_set full of descriptors, call
 * fusd_dispatch on every descriptor in the set which is a valid FUSD
 * fd.
 */
void fusd_dispatch_fdset(fd_set *set)
{
  int i;

  for (i = 0; i < FD_SETSIZE; i++)
    if (FD_ISSET(i, set) && FD_ISSET(i, &fusd_fds))
      fusd_dispatch(i);
}


/* 
 * fusd_dispatch_one() -- read a single kernel-to-userspace message
 * from fd, then call the appropriate userspace callback function,
 * based on the message that was read.  finally, return the result
 * back to the kernel, IF the return value from the callback is not
 * FUSD_NOREPLY.
 *
 * On success, returns 0.
 * On failure, returns a negative number indicating the errno.
 */
static int fusd_dispatch_one(int fd, fusd_file_operations_t *fops)
{
  fusd_file_info_t *file = NULL;
  fusd_msg_t *msg = NULL;
  int driver_retval = 0; /* returned to the FUSD driver */
  int user_retval = 0;    /* returned to the user who made the syscall */

  /* check for valid, look up ops */
  if (fops == NULL) {
    fprintf(stderr, "fusd_dispatch: no fops provided!\n");
    driver_retval = -EBADF;
    goto out_noreply;
  }

  /* allocate memory for fusd_msg_t */
  if ((msg = malloc(sizeof(fusd_msg_t))) == NULL) {
    driver_retval = -ENOMEM;
    fprintf(stderr, "libfusd: can't allocate memory\n");
    goto out_noreply;
  }
  memset(msg, '\0', sizeof(fusd_msg_t));

  /* read header and data, if it's there */
  if ((driver_retval = fusd_get_message(fd, msg)) < 0)
    goto out_noreply;

  /* allocate file info struct */
  file = malloc(sizeof(fusd_file_info_t));
  if (NULL == file) {
    fprintf(stderr, "libfusd: can't allocate memory\n");
    driver_retval = -ENOMEM;
    goto out_noreply;
  }

  /* fill the file info struct */
  memset(file, '\0', sizeof(fusd_file_info_t));
  file->fd = fd;
  file->device_info = msg->parm.fops_msg.device_info;
  file->private_data = msg->parm.fops_msg.private_info;
  file->flags = msg->parm.fops_msg.flags;
  file->pid = msg->parm.fops_msg.pid;
  file->uid = msg->parm.fops_msg.uid;
  file->gid = msg->parm.fops_msg.gid;
  file->fusd_msg = msg;  

  /* right now we only handle fops requests */
  if (msg->cmd != FUSD_FOPS_CALL && msg->cmd != FUSD_FOPS_NONBLOCK &&
      msg->cmd != FUSD_FOPS_CALL_DROPREPLY) {
    fprintf(stderr, "libfusd: got unknown msg->cmd from kernel\n");
    user_retval = -EINVAL;
    goto send_reply;
  }
  
  /* dispatch on operation type */
  user_retval = -ENOSYS;
  switch (msg->subcmd) {
  case FUSD_OPEN:
    if (fops && fops->open)
      user_retval = fops->open(file);
    break;
  case FUSD_CLOSE:
    if (fops && fops->close)
      user_retval = fops->close(file);
    break;
  case FUSD_READ:
    /* allocate a buffer and make the call */
    if (fops && fops->read) {
      if ((msg->data = malloc(msg->parm.fops_msg.length)) == NULL) {
	user_retval = -ENOMEM;
	fprintf(stderr, "libfusd: can't allocate memory\n");
      } else {
	msg->datalen = msg->parm.fops_msg.length;
	user_retval = fops->read(file, msg->data, msg->datalen,
				 &msg->parm.fops_msg.offset);
      }
    }
    break;
  case FUSD_WRITE:
    if (fops && fops->write)
      user_retval = fops->write(file, msg->data, msg->datalen,
				&msg->parm.fops_msg.offset);
    break;
  case FUSD_MMAP:
    if (fops && fops->mmap)
    {
      user_retval = fops->mmap(file, msg->parm.fops_msg.offset, msg->parm.fops_msg.length, msg->parm.fops_msg.flags,
                               &msg->parm.fops_msg.arg.ptr_arg, &msg->parm.fops_msg.length);
    }
    break;
  case FUSD_IOCTL:
    if (fops && fops->ioctl) {
      /* in the case of an ioctl read, allocate a buffer for the
       * driver to write to, IF there isn't already a buffer.  (there
       * might already be a buffer if this is a read+write) */
      if ((_IOC_DIR(msg->parm.fops_msg.cmd) & _IOC_READ) &&
	  msg->data == NULL) {
	msg->datalen = _IOC_SIZE(msg->parm.fops_msg.cmd);
	if ((msg->data = malloc(msg->datalen)) == NULL) {
	  user_retval = -ENOMEM;
	  break;
	}
      }
      if (msg->data != NULL)
	user_retval = fops->ioctl(file, msg->parm.fops_msg.cmd, msg->data);
      else
	user_retval = fops->ioctl(file, msg->parm.fops_msg.cmd,
				  (void *) msg->parm.fops_msg.arg.ptr_arg);
    }
    break;
    
  case FUSD_POLL_DIFF:
    /* This callback requests notification when an event occurs on a file,
     * e.g. becoming readable or writable */
    if (fops && fops->poll_diff)
      user_retval = fops->poll_diff(file, msg->parm.fops_msg.cmd);
    break;    
  
  case FUSD_UNBLOCK:
    /* This callback is called when a system call is interrupted */
    if (fops && fops->unblock)
      user_retval = fops->unblock(file);    
    break;
    
  default:
    fprintf(stderr, "libfusd: Got unsupported operation\n");
    user_retval = -ENOSYS;
    break;
  }
  
  goto send_reply;


  /* out_noreply is only used for handling errors */
 out_noreply:
  if (msg->data != NULL)
    free(msg->data);
  if (msg != NULL)
    free(msg);
  goto done;

  /* send_reply is only used for success */
 send_reply:
  if (-user_retval <= 0xff) {
    /* 0xff is the maximum legal return value (?) - return val to user */
    driver_retval = fusd_return(file, user_retval);
  } else {
    /* if we got a FUSD_NOREPLY, don't free the msg structure */
    driver_retval = 0;
  }

  /* this is common to both errors and success */
 done:
  if (driver_retval < 0) {
    errno = -driver_retval;
    driver_retval = -1;
  }
  return driver_retval;
}


/* fusd_dispatch is now a wrapper around fusd_dispatch_one that calls
 * it repeatedly, until it fails.  this helps a lot with bulk data
 * transfer since there is no intermediate select in between the
 * reads.  (the kernel module helps by running the user process in
 * between).
 *
 * This function now prints an error to stderr in case of error,
 * instead of returning a -1.
 */
void fusd_dispatch(int fd)
{
  int retval, num_dispatches = 0;
  fusd_file_operations_t *fops = NULL;

  /* make sure we have a valid FD, and get its fops structure */
  if (!FUSD_FD_VALID(fd)) {
    errno = EBADF;
    retval = -1;
    goto out;
  }
  fops = FUSD_GET_FOPS(fd);

  /* now keep dispatching until a dispatch returns an error */
  do {
    retval = fusd_dispatch_one(fd, fops);

    if (retval >= 0)
      num_dispatches++;
  } while (retval >= 0 && num_dispatches <= MAX_MESSAGES_PER_DISPATCH);

  /* if we've dispatched at least one message successfully, and then
   * stopped because of EAGAIN - do not report an error.  this is the
   * common case. */
  if (num_dispatches > 0 && errno == EAGAIN) {
    retval = 0;
    errno = 0;
  }

 out:
  if (retval < 0 && errno != EPIPE)
    fprintf(stderr, "libfusd: fusd_dispatch error on fd %d: %m\n", fd);
}


/*
 * fusd_destroy destroys all state associated with a fusd_file_info
 * pointer.  (It is implicitly called by fusd_return.)  If a driver
 * saves a fusd_file_info pointer by calling -FUSD_NOREPLY in order to
 * block a read, but gets a "close" request on the file before the
 * pointer is returned with fusd_return, it should be thrown away
 * using fusd_destroy.  
 */
void fusd_destroy(struct fusd_file_info *file)
{
  if (file == NULL)
    return;

  if (file->fusd_msg->data != NULL)
    free(file->fusd_msg->data);
  free(file->fusd_msg);
  free(file);
}


/*
 * construct a user-to-kernel message in reply to a file function
 * call. 
 *
 * On success, returns 0.
 * On failure, returns a negative number indicating the errno.
 */
int fusd_return(fusd_file_info_t *file, ssize_t retval)
{
  fusd_msg_t *msg = NULL;
  int fd;
  int driver_retval = 0;
  struct iovec iov[2];

  if (file == NULL) {
    fprintf(stderr, "fusd_return: NULL file\n");
    return -EINVAL;
  }

  fd = file->fd;
  if (!FUSD_FD_VALID(fd)) {
    fprintf(stderr, "fusd_return: badfd (fd %d)\n", fd);
    return -EBADF;
  }

  if ((msg = file->fusd_msg) == NULL) {
    fprintf(stderr, "fusd_return: fusd_msg is gone\n");
    return -EINVAL;
  }

  /* if this was a "DONTREPLY" message, just free the struct */
  if (msg->cmd == FUSD_FOPS_CALL_DROPREPLY)
    goto free_memory;

  /* do we copy data back to kernel?  how much? */
  switch(msg->subcmd) {
  case FUSD_READ:
    /* these operations can return data to userspace */
    if (retval > 0) {
      msg->datalen = MIN(retval, msg->parm.fops_msg.length);
      retval = msg->datalen;
    } else {
      msg->datalen = 0;
    }
    break;
  case FUSD_IOCTL:
    /* ioctl CAN (in read mode) return data to userspace */
    if ((retval == 0) && 
	(_IOC_DIR(msg->parm.fops_msg.cmd) & _IOC_READ))
      msg->datalen = _IOC_SIZE(msg->parm.fops_msg.cmd);
    else
      msg->datalen = 0;
    break;
  default:
    /* open, close, write, etc. do not return data */
    msg->datalen = 0;
    break;
  }

  /* fill the file info struct */
  msg->cmd++; /* change FOPS_CALL to FOPS_REPLY; NONBLOCK to NONBLOCK_REPLY */
  msg->parm.fops_msg.retval = retval;
  msg->parm.fops_msg.device_info = file->device_info;
  msg->parm.fops_msg.private_info = file->private_data;
  msg->parm.fops_msg.flags = file->flags;
  /* pid is NOT copied back. */

  /* send message to kernel */
  if (msg->datalen && msg->data != NULL) {
    iov[0].iov_base = msg;
    iov[0].iov_len = sizeof(fusd_msg_t);
    iov[1].iov_base = msg->data;
    iov[1].iov_len = msg->datalen;
    driver_retval = writev(fd, iov, 2);
  }
  else {
    driver_retval = write(fd, msg, sizeof(fusd_msg_t));
  }

 free_memory:
  fusd_destroy(file);

  if (driver_retval < 0)
    return -errno;
  else
    return 0;
}


/* returns static string representing the flagset (e.g. RWE) */
#define RING 5
char *fusd_unparse_flags(int flags)
{
  static int i = 0;
  static char ringbuf[RING][5];
  char *s = ringbuf[i];
  i = (i + 1) % RING;
  
  sprintf(s, "%c%c%c", 
	  (flags & FUSD_NOTIFY_INPUT)?'R':'-',
	  (flags & FUSD_NOTIFY_OUTPUT)?'W':'-',
	  (flags & FUSD_NOTIFY_EXCEPT)?'E':'-');
  
  return s;
}
#undef RING

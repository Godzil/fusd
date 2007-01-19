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
 * FUSD: the Framework for User-Space Devices
 *
 * Linux Kernel Module
 *
 * Jeremy Elson  <jelson@circlemud.org>
 * Copyright (c) 2001, Sensoria Corporation
 * Copyright (c) 2002-2003, Regents of the University of California
 * Copyright (c) 2007 Monty and Xiph.Org
 *
 * $Id$
 */

/*
 * Note on debugging messages: Unexpected errors (i.e., indicators of
 * bugs in this kernel module) should always contain '!'.  Expected
 * conditions, even if exceptional (e.g., the device-driver-provider
 * disappears while a file is waiting for a return from a system call)
 * must NOT contain '!'.
 */

#ifndef __KERNEL__
#define __KERNEL__
#endif

#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif

//#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
//#include <linux/devfs_fs_kernel.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <linux/major.h>
#include <linux/uio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/highmem.h>

#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <asm/ioctl.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

#define STATIC

/* Define this if you want to emit debug messages (adds ~8K) */
#define CONFIG_FUSD_DEBUG

/* Default debug level for FUSD messages.  Has no effect unless
 * CONFIG_FUSD_DEBUG is defined. */
#ifndef CONFIG_FUSD_DEBUGLEVEL
#define CONFIG_FUSD_DEBUGLEVEL 2
#endif

/* Define this to check for memory leaks */
/*#define CONFIG_FUSD_MEMDEBUG*/

/* Define this to use the faster wake_up_interruptible_sync instead of
 * the normal wake_up_interruptible.  Note: you can't do this unless
 * you're bulding fusd as part of the kernel (not a module); or you've
 * patched kernel/ksyms.s to add __wake_up_sync in addition to
 * __wake_up. */
/* #define CONFIG_FUSD_USE_WAKEUPSYNC */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,13)

#define CLASS class_simple
#define class_create class_simple_create
#define class_destroy class_simple_destroy
#define CLASS_DEVICE_CREATE(a, b, c, d, e) class_simple_device_add(a, c, d, e)
#define class_device_destroy(a, b) class_simple_device_remove(b)

#else

#define CLASS class

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)

#define CLASS_DEVICE_CREATE(a, b, c, d, e) class_device_create(a, c, d, e)

#else

#define CLASS_DEVICE_CREATE(a, b, c, d, e) class_device_create(a, b, c, d, e)

#endif

#endif

static inline struct kobject * to_kobj(struct dentry * dentry)
{
  struct sysfs_dirent * sd = dentry->d_fsdata;
  if(sd)
    return ((struct kobject *) sd->s_element);
  else
    return NULL;
}

#define to_class(obj) container_of(obj, struct class, subsys.kset.kobj)

/**************************************************************************/

#include "fusd.h"
#include "fusd_msg.h"
#include "kfusd.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,13)
# error "***FUSD doesn't work before Linux Kernel v2.6.13"
#endif

STATIC struct cdev* fusd_control_device;
STATIC struct cdev* fusd_status_device;

STATIC dev_t control_id;
STATIC dev_t status_id;

static struct CLASS *fusd_class;

static struct class_device *fusd_control_class_device;
static struct class_device *fusd_status_class_device;

extern struct CLASS *sound_class;

/* version number incremented for each registered device */
STATIC int last_version = 1;

/* version number incremented for each transaction to userspace */
STATIC int last_transid = 1;

/* wait queue that is awakened when new devices are registered */
STATIC DECLARE_WAIT_QUEUE_HEAD(new_device_wait);

/* the list of valid devices, and sem to protect it */
LIST_HEAD(fusd_devlist_head);
DECLARE_MUTEX(fusd_devlist_sem);

//#ifdef MODULE_LICENSE
MODULE_AUTHOR("Jeremy Elson <jelson@acm.org> (c)2001");
MODULE_LICENSE("GPL");
//#endif

/***************************Debugging Support*****************************/

#ifdef CONFIG_FUSD_DEBUG

STATIC int fusd_debug_level = CONFIG_FUSD_DEBUGLEVEL;
module_param(fusd_debug_level, int, S_IRUGO);

#define BUFSIZE 1000 /* kernel's kmalloc pool has a 1012-sized bucket */
static int debug_throttle = 0; /* emit a maximum number of debug
				  messages, else it's possible to take
				  out the machine accidentally if a
				  daemon disappears with open files */

STATIC void rdebug_real(char *fmt, ...)
{
  va_list ap;
  int len;
  char *message;

  if(debug_throttle > 100) return;
  debug_throttle++;

  /* I'm kmallocing since you don't really want 1k on the stack. I've
   * had stack overflow problems before; the kernel stack is quite
   * small... */
  if ((message = KMALLOC(BUFSIZE, GFP_KERNEL)) == NULL)
    return;

  va_start(ap, fmt);
  len = vsnprintf(message, BUFSIZE-1, fmt, ap);
  va_end(ap);

  if (len >= BUFSIZE) {
    printk("WARNING: POSSIBLE KERNEL CORRUPTION; MESSAGE TOO LONG\n");
  } else {
    printk("fusd: %.975s\n", message); /* note msgs are truncated at
                      * ~1000 chars to fit inside the 1024 printk
                      * limit imposed by the kernel */
  }

  KFREE(message);
}

#endif /* CONFIG_FUSD_DEBUG */

/******************** Memory Debugging ************************************/

#ifdef CONFIG_FUSD_MEMDEBUG

#define MAX_MEM_DEBUG 10000

DECLARE_MUTEX(fusd_memdebug_sem);

typedef struct {
  void *ptr;
  int line;
  int size;
} mem_debug_t;

mem_debug_t *mem_debug;

STATIC int fusd_mem_init(void)
{
  int i;

  mem_debug = kmalloc(sizeof(mem_debug_t) * MAX_MEM_DEBUG, GFP_KERNEL);

  if (mem_debug == NULL) {
    RDEBUG(2, "argh - memdebug malloc failed!");
    return -ENOMEM;
  }

  /* initialize */
  for (i = 0; i < MAX_MEM_DEBUG; i++)
    mem_debug[i].ptr = NULL;

  RDEBUG(2, "FUSD memory debugger activated");
  return 0;
}

STATIC void fusd_mem_cleanup(void)
{
  int i;
  int count=0;
  for (i = 0; i < MAX_MEM_DEBUG; i++)
    if (mem_debug[i].ptr != NULL) {
      RDEBUG(0, "memdebug: failed to free memory allocated at line %d (%d b)",
	     mem_debug[i].line, mem_debug[i].size);
      count++;
    }
  if (!count)
    RDEBUG(2, "congratulations - memory debugger is happy!");
  kfree(mem_debug);
}

STATIC void fusd_mem_add(void *ptr, int line, int size)
{
  int i;

  if (ptr==NULL)
    return;

  for (i = 0; i < MAX_MEM_DEBUG; i++) {
    if (mem_debug[i].ptr == NULL) {
      mem_debug[i].ptr = ptr;
      mem_debug[i].line = line;
      mem_debug[i].size = size;
      return;
    }
  }
  RDEBUG(1, "WARNING - memdebug out of space!!!!");
}

STATIC void fusd_mem_del(void *ptr)
{
  int i;
  for (i = 0; i < MAX_MEM_DEBUG; i++) {
    if (mem_debug[i].ptr == ptr) {
      mem_debug[i].ptr = NULL;
      return;
    }
  }
  RDEBUG(2, "WARNING - memdebug is confused!!!!");
}


STATIC void *fusd_kmalloc(size_t size, int type, int line)
{
  void *ptr = kmalloc(size, type);
  down(&fusd_memdebug_sem);
  fusd_mem_add(ptr, line, size);
  up(&fusd_memdebug_sem);
  return ptr;
}

STATIC void fusd_kfree(void *ptr)
{
  down(&fusd_memdebug_sem);
  fusd_mem_del(ptr);
  kfree(ptr);
  up(&fusd_memdebug_sem);
}

STATIC void *fusd_vmalloc(size_t size, int line)
{
  void *ptr = vmalloc(size);
  down(&fusd_memdebug_sem);
  fusd_mem_add(ptr, line, size);
  up(&fusd_memdebug_sem);
  return ptr;
}

STATIC void fusd_vfree(void *ptr)
{
  down(&fusd_memdebug_sem);
  fusd_mem_del(ptr);
  vfree(ptr);
  up(&fusd_memdebug_sem);
}

#endif /* CONFIG_FUSD_MEMDEBUG */


/********************* FUSD Device List ***************************/



/*************************************************************************/
/************** STATE MANAGEMENT AND BOOKKEEPING UTILITIES ***************/
/*************************************************************************/

STATIC inline void init_fusd_msg(fusd_msg_t *fusd_msg)
{
  if (fusd_msg == NULL)
    return;

  memset(fusd_msg, 0, sizeof(fusd_msg_t));
  fusd_msg->magic = FUSD_MSG_MAGIC;
  fusd_msg->cmd = FUSD_FOPS_CALL; /* typical, but can be overwritten */
}

/*
 * free a fusd_msg, and NULL out the pointer that points to that fusd_msg.
 */
STATIC inline void free_fusd_msg(fusd_msg_t **fusd_msg)
{
  if (fusd_msg == NULL || *fusd_msg == NULL)
    return;

  if ((*fusd_msg)->data != NULL) {
      VFREE((*fusd_msg)->data);
      (*fusd_msg)->data = NULL;
    }
  KFREE(*fusd_msg);
  *fusd_msg = NULL;
}


/* adjust the size of the 'files' array attached to the device to
 * better match the number of files.  In all cases, size must be at
 * least MIN_ARRAY_SIZE.  Subject to that constraint: if
 * num_files==array_size, the size is doubled; if
 * num_files<array_size/4, the size is halved.  Array is kept as is if
 * the malloc fails.  Returns a pointer to the new file struct or NULL
 * if there isn't one. */
STATIC fusd_file_t **fusd_dev_adjsize(fusd_dev_t *fusd_dev)
{
  fusd_file_t **old_array;
  int old_size;

  old_array = fusd_dev->files;
  old_size = fusd_dev->array_size;

  /* compute the new size of the array */
  if (fusd_dev->array_size > 4*fusd_dev->num_files)
    fusd_dev->array_size /= 2;
  else if (fusd_dev->array_size == fusd_dev->num_files)
    fusd_dev->array_size *= 2;

  /* respect the minimums and maximums (policy) */
  if (fusd_dev->array_size < MIN_FILEARRAY_SIZE)
    fusd_dev->array_size = MIN_FILEARRAY_SIZE;
  if (fusd_dev->array_size > MAX_FILEARRAY_SIZE)
    fusd_dev->array_size = MAX_FILEARRAY_SIZE;

  /* make sure it's sane */
  if (fusd_dev->array_size < fusd_dev->num_files) {
    RDEBUG(0, "fusd_dev_adjsize is royally screwed up!!!!!");
    return fusd_dev->files;
  }

  /* create a new array.  if successful, copy the contents of the old
   * one.  if not, revert back to the old. */
  fusd_dev->files = KMALLOC(fusd_dev->array_size * sizeof(fusd_file_t *),
			    GFP_KERNEL);
  if (fusd_dev->files == NULL) {
    RDEBUG(1, "malloc failed in fusd_dev_adjsize!");
    fusd_dev->files = old_array;
    fusd_dev->array_size = old_size;
  } else {
    RDEBUG(10, "/dev/%s now has space for %d files (had %d)", NAME(fusd_dev),
	   fusd_dev->array_size, old_size);
    memset(fusd_dev->files, 0, fusd_dev->array_size * sizeof(fusd_file_t *));
    memcpy(fusd_dev->files, old_array,
	   fusd_dev->num_files * sizeof(fusd_file_t *));
    KFREE(old_array);
  }

  return fusd_dev->files;
}


/*
 * DEVICE LOCK MUST BE HELD TO CALL THIS FUNCTION
 * 
 * This function frees a device IF there is nothing left that is
 * referencing it.
 *
 * Specifically, we do not free the device if:
 *   - The driver is still active (i.e. device is not a zombie)
 *   - There are still files with the device open
 *   - There is an open in progress, i.e. a client has verified that
 *   this is a valid device and is getting ready to add itself as an
 *   open file.
 *
 * If the device is safe to free, it is removed from the valid list
 * (in verysafe mode only) and freed.
 *
 * Returns:  1 if the device was freed
 *           0 if the device still exists (and can be unlocked) */
STATIC int maybe_free_fusd_dev(fusd_dev_t *fusd_dev)
{
  fusd_msgC_t *ptr, *next;

  down(&fusd_devlist_sem);

  /* DON'T free the device under conditions listed above */
  if (!fusd_dev->zombie || fusd_dev->num_files || fusd_dev->open_in_progress) {
    up(&fusd_devlist_sem);
    return 0;
  }

  /* OK - bombs away!  This fusd_dev_t is on its way out the door! */

  RDEBUG(8, "freeing state associated with /dev/%s", NAME(fusd_dev));

  /* delete it off the list of valid devices, and unlock */
  list_del(&fusd_dev->devlist);
  up(&fusd_devlist_sem);

  /* free any outgoing messages that the device might have waiting */
  for (ptr = fusd_dev->msg_head; ptr != NULL; ptr = next) {
    next = ptr->next;
    FREE_FUSD_MSGC(ptr);
  }
  
     /* free the device's dev name */
  if (fusd_dev->dev_name != NULL) {
    KFREE(fusd_dev->dev_name);
    fusd_dev->dev_name = NULL;
  }
  
    /* free the device's class name */
  if (fusd_dev->class_name != NULL) {
    KFREE(fusd_dev->class_name);
    fusd_dev->class_name = NULL;
  }

  /* free the device's name */
  if (fusd_dev->name != NULL) {
    KFREE(fusd_dev->name);
    fusd_dev->name = NULL;
  }
  

  /* free the array used to store pointers to fusd_file_t's */
  if (fusd_dev->files != NULL) {
    KFREE(fusd_dev->files);
    fusd_dev->files = NULL;
  }

  /* clear the structure and free it! */
  memset(fusd_dev, 0, sizeof(fusd_dev_t));
  KFREE(fusd_dev);

  /* notify fusd_status readers that there has been a change in the
   * list of registered devices */
  atomic_inc_and_ret(&last_version);
  wake_up_interruptible(&new_device_wait);

  //MOD_DEC_USE_COUNT;
  return 1;
}


/*
 *
 * DO NOT CALL THIS FUNCTION UNLESS THE DEVICE IS ALREADY LOCKED
 *
 * zombify_device: called when the driver disappears.  Indicates that
 * the driver is no longer available to service requests.  If there
 * are no outstanding system calls waiting for the fusd_dev state, the
 * device state itself is freed.
 *
 */
STATIC void zombify_dev(fusd_dev_t *fusd_dev)
{
  int i;

  if (fusd_dev->zombie) {
    RDEBUG(1, "zombify_device called on a zombie!!");
    return;
  }

  fusd_dev->zombie = 1;

  RDEBUG(3, "/dev/%s turning into a zombie (%d open files)", NAME(fusd_dev),
	 fusd_dev->num_files);

  /* If there are files holding this device open, wake them up. */
  for (i = 0; i < fusd_dev->num_files; i++) {
    wake_up_interruptible(&fusd_dev->files[i]->file_wait);
    wake_up_interruptible(&fusd_dev->files[i]->poll_wait);
  }
}



/* utility function to find the index of a fusd_file in a fusd_dev.
 * returns index if found, -1 if not found.  ASSUMES WE HAVE A VALID
 * fusd_dev.  fusd_file may be NULL if we are searching for an empty
 * slot. */
STATIC int find_fusd_file(fusd_dev_t *fusd_dev, fusd_file_t *fusd_file)
{
  int i, num_files = fusd_dev->num_files;
  fusd_file_t **files = fusd_dev->files;

  for (i = 0; i < num_files; i++)
    if (files[i] == fusd_file)
      return i;

  return -1;
}


/*
 * DEVICE LOCK MUST BE HELD BEFORE THIS IS CALLED
 *
 * Returns 1 if the device was also freed.  0 if only the file was
 * freed.  If the device is freed, then do not try to unlock it!
 * (Callers: Check the return value before unlocking!)
 */
STATIC int free_fusd_file(fusd_dev_t *fusd_dev, fusd_file_t *fusd_file)
{
  int i;
  struct list_head *tmp, *it;
   
  /* find the index of the file in the device's file-list... */
  if ((i = find_fusd_file(fusd_dev, fusd_file)) < 0)
    panic("corrupted fusd_dev: releasing a file that we think is closed");
    
  /* ...and remove it (by putting the last entry into its place) */
  fusd_dev->files[i] = fusd_dev->files[--(fusd_dev->num_files)];

  /* there might be an incoming message waiting for a restarted system
   * call.  free it -- after possibly forging a close (see
   * fusd_forge_close). */
   
   
	list_for_each_safe(it, tmp, &fusd_file->transactions)
	{
		struct fusd_transaction* transaction = list_entry(it, struct fusd_transaction, list);
		if(transaction->msg_in)
		{
      if (transaction->msg_in->subcmd == FUSD_OPEN && transaction->msg_in->parm.fops_msg.retval == 0)
        fusd_forge_close(transaction->msg_in, fusd_dev);
      free_fusd_msg(&transaction->msg_in);
		} 
		KFREE(transaction);
	}
  
  /* free state associated with this file */
  memset(fusd_file, 0, sizeof(fusd_file_t));
  KFREE(fusd_file);

  /* reduce the size of the file array if necessary */
  if (fusd_dev->array_size > MIN_FILEARRAY_SIZE &&
      fusd_dev->array_size > 4*fusd_dev->num_files)
    fusd_dev_adjsize(fusd_dev);

  /* renumber the array */
  for (i = 0; i < fusd_dev->num_files; i++)
    fusd_dev->files[i]->index = i;

  /* try to free the device -- this may have been its last file */
  return maybe_free_fusd_dev(fusd_dev);
}


/****************************************************************************/
/********************** CLIENT CALLBACK FUNCTIONS ***************************/
/****************************************************************************/


/* todo
 * fusd_restart_check: Called from the beginning of most system calls
 * to see if we are restarting a system call.
 *
 * In the common case -- that this is NOT a restarted syscall -- we
 * return 0.
 *
 * In the much less common case, we return ERESTARTSYS, and expect the
 * caller to jump right to its fusd_fops_call() call.
 *
 * In the even LESS (hopefully very rare) case when one PID had an
 * interrupted syscall, but a different PID is the next to do a system
 * call on that file descriptor -- well, we lose.  Clear state of that
 * old syscall out and continue as usual.
 */
STATIC struct fusd_transaction* fusd_find_incomplete_transaction(fusd_file_t *fusd_file, int subcmd)
{
  struct fusd_transaction* transaction = fusd_find_transaction_by_pid(fusd_file, current->pid);
  if(transaction == NULL)
    return NULL;


  if (transaction->subcmd != subcmd)
  {
      RDEBUG(2, "Incomplete transaction %ld thrown out, was expecting subcmd %d but received %d", 
	     transaction->transid, transaction->subcmd, subcmd);
    fusd_cleanup_transaction(fusd_file, transaction);
    return NULL;
  }
  
  RDEBUG(4, "pid %d restarting system call with transid %ld", current->pid,
         transaction->transid);
  return transaction;
}


STATIC int send_to_dev(fusd_dev_t *fusd_dev, fusd_msg_t *fusd_msg, int locked)
{
  fusd_msgC_t *fusd_msgC;

  /* allocate a container for the message */
  if ((fusd_msgC = KMALLOC(sizeof(fusd_msgC_t), GFP_KERNEL)) == NULL)
    return -ENOMEM;

  memset(fusd_msgC, 0, sizeof(fusd_msgC_t));
  memcpy(&fusd_msgC->fusd_msg, fusd_msg, sizeof(fusd_msg_t));

  if (!locked)
    LOCK_FUSD_DEV(fusd_dev);

  /* put the message in the device's outgoing queue.  */
  if (fusd_dev->msg_head == NULL) {
    fusd_dev->msg_head = fusd_dev->msg_tail = fusd_msgC;
  } else {
    fusd_dev->msg_tail->next = fusd_msgC;
    fusd_dev->msg_tail = fusd_msgC;
  }

  if (!locked)
    UNLOCK_FUSD_DEV(fusd_dev);

  /* wake up the driver, which now has a message waiting in its queue */
  WAKE_UP_INTERRUPTIBLE_SYNC(&fusd_dev->dev_wait);

  return 0;

 zombie_dev:
  KFREE(fusd_msgC);
  return -EPIPE;
}


/* 
 * special case: if the driver sent back a successful "open", but
 * there is no file that is actually open, we forge a "close" so that
 * the driver can maintain balanced open/close pairs.  We put calls to
 * this in fusd_fops_reply, when the reply first comes in; and,
 * free_fusd_file, when we throw away a reply that had been
 * pending for a restart.
 */
STATIC void fusd_forge_close(fusd_msg_t *msg, fusd_dev_t *fusd_dev)
{
  RDEBUG(2, "/dev/%s tried to complete an open for transid %ld, "
	 "forging a close", NAME(fusd_dev), msg->parm.fops_msg.transid);
  msg->cmd = FUSD_FOPS_CALL_DROPREPLY;
  msg->subcmd = FUSD_CLOSE;
  msg->parm.fops_msg.transid = atomic_inc_and_ret(&last_transid);
  send_to_dev(fusd_dev, msg, 1);
}



/*
 * fusd_fops_call_send: send a fusd_msg into userspace.
 *
 * NOTE - we are already holding the lock on fusd_file_arg when this
 * function is called, but NOT the lock on the fusd_dev
 */
STATIC int fusd_fops_call_send(fusd_file_t *fusd_file_arg,
			       fusd_msg_t *fusd_msg, struct fusd_transaction** transaction)
{
  fusd_dev_t *fusd_dev;
  fusd_file_t *fusd_file;

  /* I check this just in case, shouldn't be necessary. */
  GET_FUSD_FILE_AND_DEV(fusd_file_arg, fusd_file, fusd_dev);

  /* make sure message is sane */
  if ((fusd_msg->data == NULL) != (fusd_msg->datalen == 0)) {
    RDEBUG(2, "fusd_fops_call: data pointer and datalen mismatch");
    return -EINVAL;
  }

  /* fill the rest of the structure */
  fusd_msg->parm.fops_msg.pid = current->pid;
  fusd_msg->parm.fops_msg.uid = current->uid;
  fusd_msg->parm.fops_msg.gid = current->gid;
  fusd_msg->parm.fops_msg.flags = fusd_file->file->f_flags;
  fusd_msg->parm.fops_msg.offset = fusd_file->file->f_pos;
  fusd_msg->parm.fops_msg.device_info = fusd_dev->private_data;
  fusd_msg->parm.fops_msg.private_info = fusd_file->private_data;
  fusd_msg->parm.fops_msg.fusd_file = fusd_file;
  fusd_msg->parm.fops_msg.transid = atomic_inc_and_ret(&last_transid);

  /* set up certain state depending on if we expect a reply */
  switch (fusd_msg->cmd) {

  case FUSD_FOPS_CALL: /* common case */
    fusd_msg->parm.fops_msg.hint = fusd_file->index;
     
    break;

  case FUSD_FOPS_CALL_DROPREPLY:
    /* nothing needed */
    break;

  case FUSD_FOPS_NONBLOCK:
    fusd_msg->parm.fops_msg.hint = fusd_file->index;
    break;

  default:
    RDEBUG(0, "whoa - fusd_fops_call_send got msg with unknown cmd!");
    break;
  }
  
  if(transaction != NULL)
  {
    int retval;
    retval = fusd_add_transaction(fusd_file, fusd_msg->parm.fops_msg.transid, fusd_msg->subcmd,
                    fusd_msg->parm.fops_msg.length, transaction);
    if(retval < 0)
      return retval;
  }
  
  /* now add the message to the device's outgoing queue! */
  return send_to_dev(fusd_dev, fusd_msg, 0);


  /* bizarre errors go straight here */
 invalid_dev:
 invalid_file:
  RDEBUG(0, "fusd_fops_call: got invalid device or file!!!!");
  return -EPIPE;
}


/*
 * fusd_fops_call_wait: wait for a driver to reply to a message
 *
 * NOTE - we are already holding the lock on fusd_file_arg when this
 * function is called, but NOT the lock on the fusd_dev
 */
STATIC int fusd_fops_call_wait(fusd_file_t *fusd_file_arg,
			       fusd_msg_t **fusd_msg_reply, struct fusd_transaction* transaction)
{
  fusd_dev_t *fusd_dev;
  fusd_file_t *fusd_file;
  int retval;

  /* I check this just in case, shouldn't be necessary. */
  GET_FUSD_FILE_AND_DEV(fusd_file_arg, fusd_file, fusd_dev);

  /* initialize first to tell callers there is no reply (yet) */
  if (fusd_msg_reply != NULL)
    *fusd_msg_reply = NULL;

  /*
   * Now, lock the device, check for an incoming message, and sleep if
   * there is not a message already waiting for us.  Note that we are
   * unrolling the interruptible_sleep_on, as in the kernel's
   * fs/pipe.c, to avoid race conditions between checking for the
   * sleep condition and sleeping.
   */
  LOCK_FUSD_DEV(fusd_dev);
  while (transaction->msg_in == NULL) {
    DECLARE_WAITQUEUE(wait, current);

    RDEBUG(10, "pid %d blocking on transid %ld", current->pid, transaction->transid);
    current->state = TASK_INTERRUPTIBLE;
    add_wait_queue(&fusd_file->file_wait, &wait);
    UNLOCK_FUSD_DEV(fusd_dev);
    UNLOCK_FUSD_FILE(fusd_file);

    schedule();
    remove_wait_queue(&fusd_file->file_wait, &wait);
    current->state = TASK_RUNNING;

    /*
     * If we woke up due to a signal -- and not due to a reply message
     * coming in -- then we are in some trouble.  The driver is already
     * processing the request and might have changed some state that is
     * hard to roll back.  So, we'll tell the process to restart the
     * system call, and come back to this point when the system call is
     * restarted.  We need to remember the PID to avoid confusion in
     * case there is another process holding this file descriptor that
     * is also trying to make a call.
     */
    if (signal_pending(current)) {
      RDEBUG(5, "blocked pid %d got a signal; sending -ERESTARTSYS",
	     current->pid);
			LOCK_FUSD_FILE(fusd_file);
      return -ERESTARTSYS;
    }

    LOCK_FUSD_FILE(fusd_file);
    /* re-lock the device, so we can do our msg_in check again */
    LOCK_FUSD_DEV(fusd_dev);
  }
  UNLOCK_FUSD_DEV(fusd_dev);

  /* ok - at this point we are awake due to a message received. */

  if (transaction->msg_in->cmd != FUSD_FOPS_REPLY ||
      transaction->msg_in->subcmd != transaction->subcmd ||
      transaction->msg_in->parm.fops_msg.transid != transaction->transid ||
      transaction->msg_in->parm.fops_msg.fusd_file != fusd_file) {
    RDEBUG(2, "fusd_fops_call: invalid reply!");
    goto invalid_reply;
  }

  /* copy metadata back from userspace */
  fusd_file->file->f_flags = transaction->msg_in->parm.fops_msg.flags;
  fusd_file->private_data  = transaction->msg_in->parm.fops_msg.private_info;
  /* note, changes to device_info are NO LONGER honored here */

  /* if everything's okay, return the return value.  if caller is
   * willing to take responsibility for freeing the message itself, we
   * return the message too. */
  retval = transaction->msg_in->parm.fops_msg.retval;
  if (fusd_msg_reply != NULL) {
    /* NOW TRANSFERRING RESPONSIBILITY FOR FREEING THIS DATA TO THE CALLER */
    *fusd_msg_reply = transaction->msg_in;
    transaction->msg_in = NULL;
  } else {
    /* free the message ourselves */
    free_fusd_msg(&transaction->msg_in);
  }
  
  /* success */
  fusd_cleanup_transaction(fusd_file, transaction);
  return retval;

 invalid_reply:
  fusd_cleanup_transaction(fusd_file, transaction);
  return -EPIPE;

  /* bizarre errors go straight here */
 invalid_dev:
 invalid_file:
  RDEBUG(0, "fusd_fops_call: got invalid device or file!!!!");
  return -EPIPE;

 zombie_dev:
  RDEBUG(2, "fusd_fops_call: %s zombified while waiting for reply",
	 NAME(fusd_dev));
  return -EPIPE;
}


/* fusd client system call handlers should call this after they call
 * fops_call, to destroy the message that was returned to them. */
STATIC void fusd_transaction_done(struct fusd_transaction *transaction)
{
	transaction->transid = -1;
	transaction->pid = 0;
}



/********* Functions for opening a FUSD device *******************/


/*
 * The process of having a client open a FUSD device is surprisingly
 * tricky -- perhaps the most complex piece of FUSD (or, a close
 * second to poll_diffs).  Race conditions are rampant here.
 *
 * The main problem is that there is a race between clients trying to
 * open the FUSD device, and providers unregistering it (e.g., the
 * driver dying).  If the device-unregister callback starts, and is
 * scheduled out after it locks the fusd device but before it
 * unregisters the device with devfs, the open callback might be
 * invoked in this interval.  This means the client will down() on a
 * semaphore that is about to be freed when the device is destroyed.
 *
 * The only way to fix this, as far as I can tell, is for device
 * registration and unregistration to both share a global lock; the
 * client checks its 'private_data' pointer to make sure it's on the
 * list of valid devices.  If so, it sets a flag (open_in_progress)
 * which means "Don't free this device yet!".  Then, it releases the
 * global lock, grabs the device lock, and tries to add itself as a
 * "file" to the device array.  It is then safe to decrement
 * open_in_progress, because being a member of the file array will
 * guarantee that the device will zombify instead of being freed.
 *
 * Another gotcha: To avoid infinitely dining with philosophers, the
 * global lock (fusd_devlist_sem) should always be acquired AFTER a
 * fusd device is locked.  The code path that frees devices acquires
 * the device lock FIRST, so the code here must do the same.
 *
 * Because of the complexity of opening a file, I've broken it up into
 * multiple sub-functions.
 */


/*
 * fusd_dev_is_valid: If a fusd device is valid, returns 1, and will have
 * set the "open_in_progress" flag on the device.
 */
int fusd_dev_is_valid(fusd_dev_t *fusd_dev)
{
  struct list_head *tmp;
  int dev_found = 0;

  /* The first thing we must do is acquire the global lock on the
   * device list, and make sure this device is valid; if so, mark it
   * as being "in use".  If we don't do this, there's a race: after we
   * enter this function, the device may be unregistered. */
  down(&fusd_devlist_sem);
  list_for_each(tmp, &fusd_devlist_head) {
    fusd_dev_t *d = list_entry(tmp, fusd_dev_t, devlist);

    if (d == fusd_dev && d->magic == FUSD_DEV_MAGIC && !ZOMBIE(d)) {
      dev_found = 1;
      break;
    }
  }

  /* A device will not be deallocated when this counter is >0 */
  if (dev_found)
    fusd_dev->open_in_progress++;

  up(&fusd_devlist_sem);

  return dev_found;
}


int fusd_dev_add_file(struct file *file, fusd_dev_t *fusd_dev, fusd_file_t **fusd_file_ret)
{
  fusd_file_t *fusd_file;
  int i;

  /* Make sure the device didn't become a zombie while we were waiting
   * for the device lock */
  if (ZOMBIE(fusd_dev))
    return -ENOENT;

  /* this shouldn't happen.  maybe i'm insane, but i check anyway. */
  for (i = 0; i < fusd_dev->num_files; i++)
    if (fusd_dev->files[i]->file == file) {
      RDEBUG(1, "warning: fusd_client_open got open for already-open file!?");
      return -EIO;
    }

  /* You can't open your own file!  Return -EDEADLOCK if someone tries to.
   *
   * XXX - TODO - FIXME - This should eventually be more general
   * deadlock detection of arbitrary length cycles */
  if (current->pid == fusd_dev->pid) {
    RDEBUG(3, "pid %d tried to open its own device (/dev/%s)",
	   fusd_dev->pid, NAME(fusd_dev));
    return -EDEADLOCK;
  }

  /* make more space in the file array if we need it */
  if (fusd_dev->num_files == fusd_dev->array_size &&
      fusd_dev->array_size < MAX_FILEARRAY_SIZE)
    fusd_dev_adjsize(fusd_dev);

  /* make sure we have room... adjsize may have failed */
  if (fusd_dev->num_files >= fusd_dev->array_size) {
    RDEBUG(1, "/dev/%s out of state space for open files!", NAME(fusd_dev));
    return -ENOMEM;
  }

  /* create state for this file */
  if ((fusd_file = KMALLOC(sizeof(fusd_file_t), GFP_KERNEL)) == NULL) {
    RDEBUG(1, "yikes!  kernel can't allocate memory");
    return -ENOMEM;
  }
  memset(fusd_file, 0, sizeof(fusd_file_t));
  init_waitqueue_head(&fusd_file->file_wait);
  init_waitqueue_head(&fusd_file->poll_wait);
	INIT_LIST_HEAD(&fusd_file->transactions);
  init_MUTEX(&fusd_file->file_sem);
  init_MUTEX(&fusd_file->transactions_sem);
  fusd_file->last_poll_sent = -1;
  fusd_file->magic = FUSD_FILE_MAGIC;
  fusd_file->fusd_dev = fusd_dev;
  fusd_file->fusd_dev_version = fusd_dev->version;
  fusd_file->file = file;

  /* add this file to the list of files managed by the device */
  fusd_file->index = fusd_dev->num_files++;
  fusd_dev->files[fusd_file->index] = fusd_file;

  /* store the pointer to this file with the kernel */
  file->private_data = fusd_file;
  *fusd_file_ret = fusd_file;

  /* success! */
  return 0;
}

STATIC struct fusd_dev_t_s* find_user_device(int dev_id)
{
	struct list_head* entry;
	down(&fusd_devlist_sem);
	list_for_each(entry, &fusd_devlist_head)
	{
    fusd_dev_t *d = list_entry(entry, fusd_dev_t, devlist);
		if(d->dev_id == dev_id)
		{
			up(&fusd_devlist_sem);
			return d;
		}
	}
	up(&fusd_devlist_sem);
 return NULL;
}

/*
 * A client has called open() has been called on a registered device.
 * See comment higher up for detailed notes on this function.
 */
STATIC int fusd_client_open(struct inode *inode, struct file *file)
{
  int retval;
  int device_freed = 0;
  fusd_dev_t *fusd_dev = find_user_device(inode->i_rdev);
  fusd_file_t *fusd_file;
  fusd_msg_t fusd_msg;
  struct fusd_transaction* transaction;
  
  /* If the device wasn't on our valid list, stop here. */
  if (!fusd_dev_is_valid(fusd_dev))
    return -ENOENT;

  /* fusd_dev->open_in_progress now set */

  /* Lock the fusd device.  Note, when we finally do acquire the lock,
   * the device might be a zombie (driver disappeared). */
  RAWLOCK_FUSD_DEV(fusd_dev);

  RDEBUG(3, "got an open for /dev/%s (owned by pid %d) from pid %d",
	 NAME(fusd_dev), fusd_dev->pid, current->pid);

  /* Try to add ourselves to the device's file list.  If retval==0, we
     are now part of the file array.  */
  retval = fusd_dev_add_file(file, fusd_dev, &fusd_file);

  /*
   * It is now safe to unset the open_in_progress flag.  Either:
   *   1) We are part of the file array, so dev won't be freed, or;
   *   2) Something failed, so we are returning a failure now and no
   *   longer need the device.
   * Note, open_in_progress must be protected by the global sem, not
   * the device lock, due to the access of it in fusd_dev_is_valid().
   */
  down(&fusd_devlist_sem);
  fusd_dev->open_in_progress--;
  up(&fusd_devlist_sem);

  /* If adding ourselves to the device list failed, give up.  Possibly
   * free the device if it was a zombie and waiting for us to complete
   * our open. */
  if (retval < 0) {
    if (!maybe_free_fusd_dev(fusd_dev))
      UNLOCK_FUSD_DEV(fusd_dev);
    return retval;
  }

  /* send message to userspace and get retval */
  init_fusd_msg(&fusd_msg);
  fusd_msg.subcmd = FUSD_OPEN;

  /* send message to userspace and get the reply.  Device can't be
   * locked during that operation. */

  UNLOCK_FUSD_DEV(fusd_dev);
  retval = fusd_fops_call_send(fusd_file, &fusd_msg, &transaction);
  
  if (retval >= 0)
    retval = fusd_fops_call_wait(fusd_file, NULL, transaction);
  RAWLOCK_FUSD_DEV(fusd_dev);

  /* If the device zombified (while we were waiting to reacquire the
   * lock)... consider that a failure */
  if (ZOMBIE(fusd_dev))
    retval = -ENOENT;

  /* if retval is negative, throw away state... the file open failed */
  if (retval < 0) {
    RDEBUG(3, "...open failed for /dev/%s (owned by pid %d) from pid %d",
	   NAME(fusd_dev), fusd_dev->pid, current->pid);

    device_freed = free_fusd_file(fusd_dev, fusd_file);
  }

  /* Now unlock the device, if it still exists.  (It may have been
   * freed if the open failed, and we were the last outstanding
   * request for it.) */
  if (!device_freed)
    UNLOCK_FUSD_DEV(fusd_dev);

  return retval;
}


/* close() has been called on a registered device.  like
 * fusd_client_open, we must lock the entire device. */
STATIC int fusd_client_release(struct inode *inode, struct file *file)
{
  int retval;
  fusd_file_t *fusd_file;
  fusd_dev_t *fusd_dev;
  fusd_msg_t fusd_msg;
  struct fusd_transaction* transaction;
  
  GET_FUSD_FILE_AND_DEV(file->private_data, fusd_file, fusd_dev);
  LOCK_FUSD_FILE(fusd_file);

  RDEBUG(3, "got a close on /dev/%s (owned by pid %d) from pid %d",
	 NAME(fusd_dev), fusd_dev->pid, current->pid);

  /* Tell the driver that the file closed, if it still exists. */
  init_fusd_msg(&fusd_msg);
  fusd_msg.subcmd = FUSD_CLOSE;
  retval = fusd_fops_call_send(fusd_file, &fusd_msg, &transaction);
	RDEBUG(5, "fusd_client_release: send returned %d", retval);
  if (retval >= 0)
    retval = fusd_fops_call_wait(fusd_file, NULL, transaction);
	
  RDEBUG(5, "fusd_client_release: call_wait %d", retval);
  /* delete the file off the device's file-list, and free it.  note
   * that device may be a zombie right now and may be freed when we
   * come back from free_fusd_file.  we only release the lock if the
   * device still exists. */
  RAWLOCK_FUSD_DEV(fusd_dev);
  if (!free_fusd_file(fusd_dev, fusd_file)) {
    UNLOCK_FUSD_DEV(fusd_dev);
  }

  return retval;

 invalid_dev:
 invalid_file:
  RDEBUG(1, "got a close on client file from pid %d, INVALID DEVICE!",
	 current->pid);
  return -EPIPE;
}



STATIC ssize_t fusd_client_read(struct file *file , char *buf,
			 size_t count, loff_t *offset)
{
  fusd_dev_t *fusd_dev;
  fusd_file_t *fusd_file;
  struct fusd_transaction* transaction;
  fusd_msg_t fusd_msg, *reply = NULL;
  int retval = -EPIPE;

  GET_FUSD_FILE_AND_DEV(file->private_data, fusd_file, fusd_dev);

  if(ZOMBIE(fusd_dev))
    goto zombie_dev;

  LOCK_FUSD_FILE(fusd_file);

  RDEBUG(3, "got a read on /dev/%s (owned by pid %d) from pid %d",
	 NAME(fusd_dev), fusd_dev->pid, current->pid);

  transaction = fusd_find_incomplete_transaction(fusd_file, FUSD_READ);
  if (transaction && transaction->size > count)
  {
    RDEBUG(2, "Incomplete I/O transaction %ld thrown out, as the transaction's size of %d bytes was greater than "
              "the retry's size of %d bytes", transaction->transid, transaction->size, (int)count);

    fusd_cleanup_transaction(fusd_file, transaction);
    transaction = NULL;
  }

  if(transaction == NULL)
  {
    /* make sure we aren't trying to read too big of a buffer */
    if (count > MAX_RW_SIZE)
      count = MAX_RW_SIZE;
  
    /* send the message */
    init_fusd_msg(&fusd_msg);
    fusd_msg.subcmd = FUSD_READ;
    fusd_msg.parm.fops_msg.length = count;
  
    /* send message to userspace */
    if ((retval = fusd_fops_call_send(fusd_file, &fusd_msg, &transaction)) < 0)
      goto done;
  }
  
  /* and wait for the reply */
  /* todo: store and retrieve the transid from the interrupted messsage */
  retval = fusd_fops_call_wait(fusd_file, &reply, transaction);

  /* return immediately in case of error */
  if (retval < 0 || reply == NULL)
    goto done;

  /* adjust the reval if the retval indicates a larger read than the
   * data that was actually provided */
  if (reply->datalen != retval) {
    RDEBUG(1, "warning: /dev/%s driver (pid %d) claimed it returned %d bytes "
	   "on read but actually returned %d", 
	   NAME(fusd_dev), fusd_dev->pid, retval, reply->datalen);
    retval = reply->datalen;
  }

  /* adjust if the device driver gave us more data than the user asked for
   *     (bad!  bad!  why is the driver broken???) */
  if (retval > count) {
    RDEBUG(1, "warning: /dev/%s driver (pid %d) returned %d bytes on read but "
	   "the user only asked for %d", 
	   NAME(fusd_dev), fusd_dev->pid, retval, (int) count);
    retval = count;
  }

  /* copy the offset back from the message */
  *offset = reply->parm.fops_msg.offset;

  /* IFF return value indicates data present, copy it back */
  if (retval > 0) {
    if (copy_to_user(buf, reply->data, retval)) {
      retval = -EFAULT;
      goto done;
    }
  }

 done:
  /* clear the readable bit of our cached poll state */
  fusd_file->cached_poll_state &= ~(FUSD_NOTIFY_INPUT);

  free_fusd_msg(&reply);
  UNLOCK_FUSD_FILE(fusd_file);
  return retval;

 invalid_file:
 invalid_dev:
 zombie_dev:
  RDEBUG(3, "got a read on client file from pid %d, driver has disappeared",
	 current->pid);
  return -EPIPE;
}

STATIC int fusd_add_transaction(fusd_file_t *fusd_file, int transid, int subcmd, int size, struct fusd_transaction** out_transaction)
{
	struct fusd_transaction* transaction = (struct fusd_transaction*) KMALLOC(sizeof(struct fusd_transaction), GFP_KERNEL);
	if(transaction == NULL)
		return -ENOMEM;
	
	transaction->msg_in = NULL;
	transaction->transid = transid;
	transaction->subcmd = subcmd;
	transaction->pid = current->pid;
	transaction->size = size;
	
	down(&fusd_file->transactions_sem);
	list_add_tail(&transaction->list, &fusd_file->transactions);
	up(&fusd_file->transactions_sem);
	
	if(out_transaction != NULL)
		*out_transaction = transaction;
	
	return 0;
}

STATIC void fusd_cleanup_transaction(fusd_file_t *fusd_file, struct fusd_transaction* transaction)
{
  free_fusd_msg(&transaction->msg_in);
  fusd_remove_transaction(fusd_file, transaction);
}

STATIC void fusd_remove_transaction(fusd_file_t *fusd_file, struct fusd_transaction* transaction)
{
	down(&fusd_file->transactions_sem);
	list_del(&transaction->list);
	up(&fusd_file->transactions_sem);
	
	KFREE(transaction);
}

STATIC struct fusd_transaction* fusd_find_transaction(fusd_file_t *fusd_file, int transid)
{
	struct list_head* i;
	down(&fusd_file->transactions_sem);
	list_for_each(i, &fusd_file->transactions)
	{
		struct fusd_transaction* transaction = list_entry(i, struct fusd_transaction, list);
		if(transaction->transid == transid)
		{
			up(&fusd_file->transactions_sem);
			return transaction;
		}
	}
	up(&fusd_file->transactions_sem);
	return NULL;
}

STATIC struct fusd_transaction* fusd_find_transaction_by_pid(fusd_file_t *fusd_file, int pid)
{
	struct list_head* i;
	down(&fusd_file->transactions_sem);
	list_for_each(i, &fusd_file->transactions)
	{
		struct fusd_transaction* transaction = list_entry(i, struct fusd_transaction, list);
		if(transaction->pid == pid)
		{
			up(&fusd_file->transactions_sem);
			return transaction;
		}
	}
	up(&fusd_file->transactions_sem);
	return NULL;
}

STATIC ssize_t fusd_client_write(struct file *file,
    const char *buffer,
    size_t length,
    loff_t *offset)
{
  fusd_dev_t *fusd_dev;
  fusd_file_t *fusd_file;
  fusd_msg_t fusd_msg;
  fusd_msg_t *reply = NULL;
  int retval = -EPIPE;
  struct fusd_transaction* transaction;
  
  GET_FUSD_FILE_AND_DEV(file->private_data, fusd_file, fusd_dev);

  if(ZOMBIE(fusd_dev))
    goto zombie_dev;

  LOCK_FUSD_FILE(fusd_file);

  RDEBUG(3, "got a write on /dev/%s (owned by pid %d) from pid %d",
	 NAME(fusd_dev), fusd_dev->pid, current->pid);

  transaction = fusd_find_incomplete_transaction(fusd_file, FUSD_WRITE);
  if (transaction && transaction->size != length)
  {
    RDEBUG(2, "Incomplete I/O transaction %ld thrown out, as the transaction's size of %d bytes was not equal to "
              "the retry's size of %d bytes", transaction->transid, transaction->size, (int) length);

    fusd_cleanup_transaction(fusd_file, transaction);
    transaction = NULL;
  }
  if(transaction == NULL)
  {
    if (length < 0) {
      RDEBUG(2, "fusd_client_write: got invalid length %d", (int) length);
      retval = -EINVAL;
      goto done;
    }
  
    if (length > MAX_RW_SIZE)
      length = MAX_RW_SIZE;
  
    init_fusd_msg(&fusd_msg);
  
    /* sigh.. i guess zero length writes should be legal */
    if (length > 0) {
      if ((fusd_msg.data = VMALLOC(length)) == NULL) {
        retval = -ENOMEM;
        goto done;
      }
  
      if (copy_from_user(fusd_msg.data, buffer, length)) {
        retval = -EFAULT;
        goto done;
      }
      fusd_msg.datalen = length;
    }
    
    fusd_msg.subcmd = FUSD_WRITE;
    fusd_msg.parm.fops_msg.length = length;
    
    if ((retval = fusd_fops_call_send(fusd_file, &fusd_msg, &transaction)) < 0)
      goto done;
  }
	/* todo: fix transid on restart */
  retval = fusd_fops_call_wait(fusd_file, &reply, transaction);

  if (retval < 0 || reply == NULL)
    goto done;

  /* drivers should not write more bytes than they were asked to! */
  if (retval > length) {
    RDEBUG(1, "warning: /dev/%s driver (pid %d) returned %d bytes on write; "
	   "the user only wanted %d", 
	   NAME(fusd_dev), fusd_dev->pid, retval, (int) length);
    retval = length;
  }

  *offset = reply->parm.fops_msg.offset;

  /* all done! */

 done:
  /* clear the writable bit of our cached poll state */
  fusd_file->cached_poll_state &= ~(FUSD_NOTIFY_OUTPUT);

  free_fusd_msg(&reply);
  UNLOCK_FUSD_FILE(fusd_file);
  return retval;

 invalid_file:
 invalid_dev:
 zombie_dev:
  RDEBUG(3, "got a write on client file from pid %d, driver has disappeared",
	 current->pid);
  return -EPIPE;
}


STATIC int fusd_client_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, unsigned long arg)
{
  fusd_dev_t *fusd_dev;
  fusd_file_t *fusd_file;
  fusd_msg_t fusd_msg, *reply = NULL;
  int retval = -EPIPE, dir, length;
  struct fusd_transaction* transaction;
  
  GET_FUSD_FILE_AND_DEV(file->private_data, fusd_file, fusd_dev);

  if(ZOMBIE(fusd_dev))
    goto zombie_dev;

  LOCK_FUSD_FILE(fusd_file);

  RDEBUG(3, "got an ioctl on /dev/%s (owned by pid %d) from pid %d",
	 NAME(fusd_dev), fusd_dev->pid, current->pid);

  dir = _IOC_DIR(cmd);
  length = _IOC_SIZE(cmd);

  transaction = fusd_find_incomplete_transaction(fusd_file, FUSD_IOCTL);
  // todo: Check to make sure the transaction is for the same IOCTL

  if(transaction == NULL)
  {
    /* if we're trying to read or write, make sure length is sane */
    if ((dir & (_IOC_WRITE | _IOC_READ)) &&
        (length <= 0 || length > MAX_RW_SIZE))
      {
        RDEBUG(2, "client ioctl got crazy IOC_SIZE of %d", length);
        retval = -EINVAL;
        goto done;
      }
  
    /* fill the struct */
    init_fusd_msg(&fusd_msg);
    fusd_msg.subcmd = FUSD_IOCTL;
    fusd_msg.parm.fops_msg.cmd = cmd;
    fusd_msg.parm.fops_msg.arg.arg = arg;
  
    /* get the data if user is trying to write to the driver */
    if (dir & _IOC_WRITE) {
      if ((fusd_msg.data = VMALLOC(length)) == NULL) {
        RDEBUG(2, "can't vmalloc for client ioctl!");
        retval = -ENOMEM;
        goto done;
      }
  
      if (copy_from_user(fusd_msg.data, (void *) arg, length)) {
        retval = -EFAULT;
        goto done;
      }
      fusd_msg.datalen = length;
    }
  
    /* send request to the driver */
    if ((retval = fusd_fops_call_send(fusd_file, &fusd_msg, &transaction)) < 0)
      goto done;
  }
  /* get the response */
	/* todo: fix transid on restart */
  if ((retval = fusd_fops_call_wait(fusd_file, &reply, transaction)) < 0 || reply == NULL)
    goto done;

  /* if user is trying to read from the driver, copy data back */
  if (dir & _IOC_READ) {
    if (reply->data == NULL || reply->datalen != length) {
      RDEBUG(2, "client_ioctl read reply with screwy data (%d, %d)",
	     reply->datalen, length);
      retval = -EIO;
      goto done;
    }
    if (copy_to_user((void *)arg, reply->data, length)) {
      retval = -EFAULT;
      goto done;
    }
  }

  /* all done! */
 done:
  free_fusd_msg(&reply);
  UNLOCK_FUSD_FILE(fusd_file);
  return retval;

 invalid_file:
 invalid_dev:
 zombie_dev:
  RDEBUG(3, "got an ioctl on client file from pid %d, driver has disappeared",
	 current->pid);
  return -EPIPE;
}
static void fusd_client_mm_open(struct vm_area_struct * vma);
static void fusd_client_mm_close(struct vm_area_struct * vma);
static struct page* fusd_client_nopage(struct vm_area_struct* vma, unsigned long address, int* type);
static struct vm_operations_struct fusd_remap_vm_ops =
{
  open: fusd_client_mm_open,
  close: fusd_client_mm_close,
  nopage: fusd_client_nopage,
};

struct fusd_mmap_instance
{
  fusd_dev_t* fusd_dev;
  fusd_file_t* fusd_file;
  unsigned long addr;
  int size;
  atomic_t refcount;
};

static void fusd_client_mm_open(struct vm_area_struct * vma)
{
  struct fusd_mmap_instance* mmap_instance = (struct fusd_mmap_instance*) vma->vm_private_data;
  atomic_inc(&mmap_instance->refcount);
  
}

static void fusd_client_mm_close(struct vm_area_struct * vma)
{
  struct fusd_mmap_instance* mmap_instance = (struct fusd_mmap_instance*) vma->vm_private_data;
  if(atomic_dec_and_test(&mmap_instance->refcount))
  {
    KFREE(mmap_instance);
  }
}

static int fusd_client_mmap(struct file *file, struct vm_area_struct * vma)
{
  fusd_dev_t *fusd_dev;
  fusd_file_t *fusd_file;
  struct fusd_transaction* transaction;
  fusd_msg_t fusd_msg, *reply = NULL;
  int retval = -EPIPE;
  struct fusd_mmap_instance* mmap_instance;

  GET_FUSD_FILE_AND_DEV(file->private_data, fusd_file, fusd_dev);

  if(ZOMBIE(fusd_dev))
    goto zombie_dev;

  LOCK_FUSD_FILE(fusd_file);

  RDEBUG(3, "got a mmap on /dev/%s (owned by pid %d) from pid %d",
	 NAME(fusd_dev), fusd_dev->pid, current->pid);

  transaction = fusd_find_incomplete_transaction(fusd_file, FUSD_MMAP);

  if(transaction == NULL)
  {
    /* send the message */
    init_fusd_msg(&fusd_msg);
    fusd_msg.subcmd = FUSD_MMAP;
    fusd_msg.parm.fops_msg.offset = vma->vm_pgoff << PAGE_SHIFT;
    fusd_msg.parm.fops_msg.flags = vma->vm_flags;
    fusd_msg.parm.fops_msg.length = vma->vm_end - vma->vm_start;
    
    /* send message to userspace */
    if ((retval = fusd_fops_call_send(fusd_file, &fusd_msg, &transaction)) < 0)
      goto done;
  }
  
  /* and wait for the reply */
  /* todo: store and retrieve the transid from the interrupted messsage */
  retval = fusd_fops_call_wait(fusd_file, &reply, transaction);
  
  mmap_instance = 
    (struct fusd_mmap_instance*) KMALLOC(sizeof(struct fusd_mmap_instance), GFP_KERNEL);
  // todo: free this thing at some point
  
  mmap_instance->fusd_dev = fusd_dev;
  mmap_instance->fusd_file = fusd_file;
  mmap_instance->addr = reply->parm.fops_msg.arg.arg;
  mmap_instance->size = reply->parm.fops_msg.length;
  atomic_set(&mmap_instance->refcount, 0);
  
  retval = reply->parm.fops_msg.retval;
  
  vma->vm_private_data = mmap_instance;
  vma->vm_ops = &fusd_remap_vm_ops;
  vma->vm_flags |= VM_RESERVED;
  
  fusd_client_mm_open(vma);
  
 done:
  free_fusd_msg(&reply);
  UNLOCK_FUSD_FILE(fusd_file);
  return retval;

 invalid_file:
 invalid_dev:
 zombie_dev:
  RDEBUG(3, "got a mmap on client file from pid %d, driver has disappeared",
	 current->pid);
  return -EPIPE;
}

static struct page* fusd_client_nopage(struct vm_area_struct* vma, unsigned long address,
                                int* type)
{
  struct fusd_mmap_instance* mmap_instance = (struct fusd_mmap_instance*) vma->vm_private_data;
  unsigned long offset;
  struct page *page = NOPAGE_SIGBUS;
  int result;
  offset = (address - vma->vm_start) + (vma->vm_pgoff << PAGE_SHIFT);
  // todo: worry about size
  if(offset > mmap_instance->size)
    goto out;
  
  down_read(&mmap_instance->fusd_dev->task->mm->mmap_sem);
  result = get_user_pages(mmap_instance->fusd_dev->task, mmap_instance->fusd_dev->task->mm, mmap_instance->addr + offset, 1, 1, 0, &page, NULL);
  up_read(&mmap_instance->fusd_dev->task->mm->mmap_sem);
  
  
  if(PageAnon(page))
  {
    RDEBUG(2, "Cannot mmap anonymous pages. Be sure to allocate your shared buffer with MAP_SHARED | MAP_ANONYMOUS");
    return NOPAGE_SIGBUS;
  }
  
  if(result > 0)
  {
    get_page(page);
    if (type)
      *type = VM_FAULT_MINOR;
  }
out:
  return page;


}


/*
 * The design of poll for clients is a bit subtle.
 *
 * We don't want the select() call itself to block, so we keep a cache
 * of the most recently known state supplied by the driver.  The cache
 * is initialized to 0 (meaning: nothing readable/writable).
 *
 * When a poll comes in, we do a non-blocking (!) dispatch of a
 * command telling the driver "This is the state we have cached, reply
 * to this call when the state changes.", and then immediately return
 * the cached state.  We tell the kernel's select to sleep on our
 * poll_wait wait queue.
 *
 * When the driver replies, we update our cached info and wake up the
 * wait queue.  Waking up the wait queue will most likely immediately
 * effect a poll again, in which case we will reply whatever we just
 * cached from the driver.
 * 
 */
STATIC unsigned int fusd_client_poll(struct file *file, poll_table *wait)
{
  fusd_dev_t *fusd_dev;
  fusd_file_t *fusd_file;
  int kernel_bits = 0;
  int send_poll = 0;

  GET_FUSD_FILE_AND_DEV(file->private_data, fusd_file, fusd_dev);
  LOCK_FUSD_FILE(fusd_file);
  LOCK_FUSD_DEV(fusd_dev);

  RDEBUG(3, "got a select on /dev/%s (owned by pid %d) from pid %d, cps=%d",
	 NAME(fusd_dev), fusd_dev->pid, current->pid,
	 fusd_file->cached_poll_state);

  poll_wait(file, &fusd_file->poll_wait, wait);

  /*
   * If our currently cached poll state is not the same as the
   * most-recently-sent polldiff request, then, dispatch a new
   * request.  (We DO NOT wait for a reply, but just dispatch the
   * request).
   *
   * Also, don't send a new polldiff if the most recent one resulted
   * in an error.
   */
  if (fusd_file->last_poll_sent != fusd_file->cached_poll_state &&
      fusd_file->cached_poll_state >= 0) {
    RDEBUG(3, "sending polldiff request because lps=%d, cps=%d",
	   fusd_file->last_poll_sent, fusd_file->cached_poll_state);
    send_poll = 1;
    fusd_file->last_poll_sent = fusd_file->cached_poll_state;
  }

  /* compute what to return for the state we had cached, converting to
   * bits that have meaning to the kernel */
  if (fusd_file->cached_poll_state > 0) {
    if (fusd_file->cached_poll_state & FUSD_NOTIFY_INPUT)
      kernel_bits |= POLLIN;
    if (fusd_file->cached_poll_state & FUSD_NOTIFY_OUTPUT)
      kernel_bits |= POLLOUT;
    if (fusd_file->cached_poll_state & FUSD_NOTIFY_EXCEPT)
      kernel_bits |= POLLPRI;
  }

  /* Now that we've committed to sending the poll, etc., it should be
   * safe to unlock the device */
  UNLOCK_FUSD_DEV(fusd_dev);
  UNLOCK_FUSD_FILE(fusd_file);

  if (send_poll) {
    fusd_msg_t fusd_msg;

    init_fusd_msg(&fusd_msg);
    fusd_msg.cmd = FUSD_FOPS_NONBLOCK;
    fusd_msg.subcmd = FUSD_POLL_DIFF;
    fusd_msg.parm.fops_msg.cmd = fusd_file->cached_poll_state;
    if (fusd_fops_call_send(fusd_file, &fusd_msg, NULL) < 0) {
      /* If poll dispatched failed, set back to -1 so we try again.
       * Not a race (I think), since sending an *extra* polldiff never
       * hurts anything. */
      fusd_file->last_poll_sent = -1;
    }
  }
  return kernel_bits;

 zombie_dev:
  /* might jump here from LOCK_FUSD_DEV */
  UNLOCK_FUSD_FILE(fusd_file);
 invalid_dev:
 invalid_file:
  RDEBUG(3, "got a select on client file from pid %d, driver has disappeared",
	 current->pid);
  return POLLPRI;
}



STATIC struct file_operations fusd_client_fops = {
  owner:    THIS_MODULE,
  open:     fusd_client_open,
  release:  fusd_client_release,
  read:     fusd_client_read,
  write:    fusd_client_write,
  ioctl:    fusd_client_ioctl,
  poll:     fusd_client_poll,
  mmap:     fusd_client_mmap
};


/*************************************************************************/
/*************************************************************************/
/*************************************************************************/


STATIC fusd_file_t *find_fusd_reply_file(fusd_dev_t *fusd_dev, fusd_msg_t *msg)
{
  /* first, try the hint */
  int i = msg->parm.fops_msg.hint;
  if (i >= 0 &&
      i < fusd_dev->num_files &&
      fusd_dev->files[i] == msg->parm.fops_msg.fusd_file)
    {
      RDEBUG(15, "find_fusd_reply_file: hint worked");
    } else {
      /* hint didn't work, fall back to a search of the whole array */
      i = find_fusd_file(fusd_dev, msg->parm.fops_msg.fusd_file);
      RDEBUG(15, "find_fusd_reply_file: hint failed");
    }

  /* we couldn't find anyone waiting for this message! */
  if (i < 0) {
    return NULL;
  } else {
    return fusd_dev->files[i];
  }
}


/* Process an incoming reply to a message dispatched by
 * fusd_fops_call.  Called by fusd_write when a driver writes to
 * /dev/fusd. */
STATIC int fusd_fops_reply(fusd_dev_t *fusd_dev, fusd_msg_t *msg)
{
  fusd_file_t *fusd_file;
  struct fusd_transaction *transaction;

  /* figure out the index of the file we are replying to.  usually
   * very fast (uses a hint) */
  if ((fusd_file = find_fusd_reply_file(fusd_dev, msg)) == NULL) {
    RDEBUG(2, "fusd_fops_reply: got a reply on /dev/%s with no connection",
	   NAME(fusd_dev));
    goto discard;
  }

  /* make sure this is not an old reply going to an old instance that's gone */
	/* todo: kor fix this */
/*
  if (fusd_file->fusd_dev_version != fusd_dev->version ||
      msg->parm.fops_msg.transid != fusd_file->transid_outstanding) {
    RDEBUG(2, "fusd_fops_reply: got an old message, discarding");
    goto discard;
  }*/
  
  transaction = fusd_find_transaction(fusd_file, msg->parm.fops_msg.transid);
	if(transaction == NULL)
	{
		RDEBUG(2, "fusd_fops_reply: No transaction found with transid %ld", msg->parm.fops_msg.transid);
		goto discard;
	}
	
  RDEBUG(10, "fusd_fops_reply: /dev/%s completed transid %ld (retval %d)",
	 NAME(fusd_dev), msg->parm.fops_msg.transid,
	 (int) msg->parm.fops_msg.retval);

  transaction->msg_in = msg;
	mb();

  WAKE_UP_INTERRUPTIBLE_SYNC(&fusd_file->file_wait);

  return 0;

 discard:
  if (msg->subcmd == FUSD_OPEN && msg->parm.fops_msg.retval == 0) {
    fusd_forge_close(msg, fusd_dev);
    return 0;
  } else {
    return -EPIPE;
  }
}


/* special function to process responses to POLL_DIFF */
STATIC int fusd_polldiff_reply(fusd_dev_t *fusd_dev, fusd_msg_t *msg)
{
  fusd_file_t *fusd_file;

  /* figure out the index of the file we are replying to.  usually
   * very fast (uses a hint) */
  if ((fusd_file = find_fusd_reply_file(fusd_dev, msg)) == NULL)
    return -EPIPE;

  /* record the poll state returned.  convert all negative retvals to -1. */
  if ((fusd_file->cached_poll_state = msg->parm.fops_msg.retval) < 0)
    fusd_file->cached_poll_state = -1;

  RDEBUG(3, "got updated poll state from /dev/%s driver: %d", NAME(fusd_dev),
	 fusd_file->cached_poll_state);

  /* since the client has returned the polldiff we sent, set
   * last_poll_sent to -1, so that we'll send a polldiff request on
   * the next select. */
  fusd_file->last_poll_sent = -1;

  /* wake up select's queue so that a new polldiff is generated */
  wake_up_interruptible(&fusd_file->poll_wait);

  return 0;
}

STATIC int systest (struct super_block *sb,void *data){
  return 1;

}

STATIC int fusd_register_device(fusd_dev_t *fusd_dev,
				register_msg_t register_msg)
{
  int error = 0;
  struct list_head *tmp;
  int dev_id;

  /* make sure args are valid */
  if (fusd_dev == NULL) {
    RDEBUG(0, "fusd_register_device: bug in arguments!");
    return -EINVAL;
  }

  register_msg.name[FUSD_MAX_NAME_LENGTH] = '\0';

  /* make sure that there isn't already a device by this name */
  down(&fusd_devlist_sem);
  list_for_each(tmp, &fusd_devlist_head) {
    fusd_dev_t *d = list_entry(tmp, fusd_dev_t, devlist);
    

    if (d && d->name && !d->zombie && !strcmp(d->name, register_msg.name)) {
      error = -EEXIST;
      break;
    }
  }
  up(&fusd_devlist_sem);

  if (error)
    return error;


  /* allocate memory for the name, and copy */
  if ((fusd_dev->name = KMALLOC(strlen(register_msg.name)+1, GFP_KERNEL)) == NULL) {
     RDEBUG(1, "yikes!  kernel can't allocate memory");
     return -ENOMEM;
  }
	
  strcpy(fusd_dev->name, register_msg.name);

  /* allocate memory for the class name, and copy */
  if ((fusd_dev->class_name = KMALLOC(strlen(register_msg.clazz)+1, GFP_KERNEL)) == NULL) {
     RDEBUG(1, "yikes!  kernel can't allocate memory");
     return -ENOMEM;
  }
	
  strcpy(fusd_dev->class_name, register_msg.clazz);
	
  /* allocate memory for the class name, and copy */
  if ((fusd_dev->dev_name = KMALLOC(strlen(register_msg.devname)+1, GFP_KERNEL)) == NULL) {
     RDEBUG(1, "yikes!  kernel can't allocate memory");
     return -ENOMEM;
  }
  
  strcpy(fusd_dev->dev_name, register_msg.devname);
  
  dev_id = 0;
  
  if((error = alloc_chrdev_region(&dev_id, 0, 1, fusd_dev->name)) < 0)
    {
      printk(KERN_ERR "alloc_chrdev_region failed status: %d\n", error);
      goto register_failed;
    }
  
  fusd_dev->dev_id = dev_id;
  
  fusd_dev->handle = cdev_alloc();
  if(fusd_dev->handle == NULL)
    {
      printk(KERN_ERR "cdev_alloc() failed\n");
      error = -ENOMEM;
      goto register_failed3;
    }
  
  fusd_dev->handle->owner = THIS_MODULE;
  fusd_dev->handle->ops = &fusd_client_fops;
  
  kobject_set_name(&fusd_dev->handle->kobj, fusd_dev->name);
  
  if((error = cdev_add(fusd_dev->handle, dev_id, 1)) < 0)
    {
      printk(KERN_ERR "cdev_add failed status: %d\n", error);
      kobject_put(&fusd_dev->handle->kobj);
      goto register_failed3;
    }

  /* look up class in sysfs */

  {
    struct CLASS *sys_class = NULL;
    struct file_system_type *sysfs = get_fs_type("sysfs");
    struct dentry *classdir = NULL;
    struct dentry *classdir2 = NULL;
    struct super_block *sb = NULL;
 
    if(sysfs){      
      sb = sget (sysfs, systest, NULL,NULL);
      
      /* because put_filesystem isn't exported */
      module_put(sysfs->owner);

      if(sb){
	struct dentry *root = sb->s_root;

	if(root){
	  struct qstr name;

	  name.name = "class";
	  name.len = 5;
	  name.hash = full_name_hash(name.name, name.len);
	  classdir = d_lookup(root, &name);
	      
	  if(classdir){
	    name.name = register_msg.clazz;
	    name.len = strlen(name.name);
	    name.hash = full_name_hash(name.name, name.len);
	    classdir2 = d_lookup(classdir, &name);
	    
	    if(classdir2){
	      // jackpot.  extract the class.
	      struct kobject *ko = to_kobj(classdir2);
	      sys_class = (ko?to_class(ko):NULL);

	      if(!sys_class)
		RDEBUG(2, "WARNING: sysfs entry for %s has no kobject!\n",register_msg.clazz);
	    }
	  }else{
	    RDEBUG(2, "WARNING: sysfs does not list a class directory!\n");
	  }
	}else{
	  RDEBUG(2, "WARNING: unable to access root firectory in sysfs!\n");
	}
      }else{
	RDEBUG(2, "WARNING: unable to access superblock for sysfs!\n");
      }
    }else{
      RDEBUG(2, "WARNING: sysfs not mounted or unavailable!\n");
    }
    
    if(sys_class){
      RDEBUG(3, "Found entry for class '%s' in sysfs\n",register_msg.clazz);
      fusd_dev->clazz = sound_class;
      fusd_dev->owns_class = 0;
    }else{
      RDEBUG(3, "Sysfs has no entry for '%s'; registering new class\n",register_msg.clazz);
      fusd_dev->clazz = class_create(THIS_MODULE, fusd_dev->class_name);
      if(IS_ERR(fusd_dev->clazz))
	{
	  error = PTR_ERR(fusd_dev->clazz);
	  printk(KERN_ERR "class_create failed status: %d\n", error);
	  goto register_failed4;
	}
      fusd_dev->owns_class = 1;
    }

    if(classdir)
      dput(classdir);
    if(classdir2)
      dput(classdir2);
    
    if(sb){
      up_write(&sb->s_umount);
      deactivate_super(sb);
    }
  }
  
  fusd_dev->class_device = CLASS_DEVICE_CREATE(fusd_dev->clazz, NULL, fusd_dev->dev_id, NULL, fusd_dev->dev_name);
  if(fusd_dev->class_device == NULL)
    {
      error = PTR_ERR(fusd_dev->class_device);
      printk(KERN_ERR "class_device_create failed status: %d\n", error);
      goto register_failed5;
    }
  
  /* make sure the registration was successful */
  if (fusd_dev->handle == 0) {
    error = -EIO;
    goto register_failed;
  }
  
  /* remember the user's private data so we can pass it back later */
  fusd_dev->private_data = register_msg.device_info;
  
  /* everything ok */
  fusd_dev->version = atomic_inc_and_ret(&last_version);
  RDEBUG(3, "pid %d registered /dev/%s v%ld", fusd_dev->pid, NAME(fusd_dev),
	 fusd_dev->version);
  wake_up_interruptible(&new_device_wait);
  return 0;

register_failed5:
	class_destroy(fusd_dev->clazz);
	fusd_dev->clazz = NULL;
register_failed4:
	cdev_del(fusd_dev->handle);
	fusd_dev->handle = NULL;
register_failed3:

	//register_failed2:
	unregister_chrdev_region(dev_id, 1);
register_failed:
  KFREE(fusd_dev->name);
  fusd_dev->name = NULL;
  return error;
}


/****************************************************************************/
/******************** CONTROL CHANNEL CALLBACK FUNCTIONS ********************/
/****************************************************************************/


/* open() called on /dev/fusd itself */
STATIC int fusd_open(struct inode *inode, struct file *file)
{
  fusd_dev_t *fusd_dev = NULL;
  fusd_file_t **file_array = NULL;

  /* keep the module from being unloaded during initialization! */
  //MOD_INC_USE_COUNT;

  /* allocate memory for the device state */
  if ((fusd_dev = KMALLOC(sizeof(fusd_dev_t), GFP_KERNEL)) == NULL)
    goto dev_malloc_failed;
  memset(fusd_dev, 0, sizeof(fusd_dev_t));

  if ((file_array = fusd_dev_adjsize(fusd_dev)) == NULL)
    goto file_malloc_failed;

  init_waitqueue_head(&fusd_dev->dev_wait);
  init_MUTEX(&fusd_dev->dev_sem);
  fusd_dev->magic = FUSD_DEV_MAGIC;
  fusd_dev->pid = current->pid;
  fusd_dev->task = current;
  file->private_data = fusd_dev;

  /* add to the list of valid devices */
  down(&fusd_devlist_sem);
  list_add(&fusd_dev->devlist, &fusd_devlist_head);
  up(&fusd_devlist_sem);

  RDEBUG(3, "pid %d opened /dev/fusd", fusd_dev->pid);
  return 0;

 file_malloc_failed:
  KFREE(fusd_dev);
 dev_malloc_failed:
  RDEBUG(1, "out of memory in fusd_open!");
  //MOD_DEC_USE_COUNT;
  return -ENOMEM;
}


/* close() called on /dev/fusd itself.  destroy the device that
 * was registered by it, if any. */
STATIC int fusd_release(struct inode *inode, struct file *file)
{
  fusd_dev_t *fusd_dev;

  GET_FUSD_DEV(file->private_data, fusd_dev);
  LOCK_FUSD_DEV(fusd_dev);

  if (fusd_dev->pid != current->pid) {
    RDEBUG(2, "yikes!: when releasing device, pid mismatch");
  }

  RDEBUG(3, "pid %d closing /dev/fusd", current->pid);

#if 0
  /* This delay is needed to exercise the openrace.c race condition,
   * i.e. testing to make sure that our open_in_progress stuff works */
  {
    int target = jiffies + 10*HZ;

    RDEBUG(1, "starting to wait");
    while (jiffies < target)
      schedule();
    RDEBUG(1, "stopping wait");
  }
#endif

  if(fusd_dev->handle){
    class_device_destroy(fusd_dev->clazz, fusd_dev->dev_id);
    if(fusd_dev->owns_class)
      {
	class_destroy(fusd_dev->clazz);
      }
    cdev_del(fusd_dev->handle);
    unregister_chrdev_region(fusd_dev->dev_id, 1);
  }

  /* mark the driver as being gone */
  zombify_dev(fusd_dev);

  /* ...and possibly free it.  (Release lock if it hasn't been freed) */
  if (!maybe_free_fusd_dev(fusd_dev))
    UNLOCK_FUSD_DEV(fusd_dev);

  /* notify fusd_status readers that there has been a change in the
   * list of registered devices */
  atomic_inc_and_ret(&last_version);
  wake_up_interruptible(&new_device_wait);

  return 0;

 zombie_dev:
 invalid_dev:
  RDEBUG(1, "invalid device found in fusd_release!!");
  return -ENODEV;
}


/*
 * This function processes messages coming from userspace device drivers
 * (i.e., writes to the /dev/fusd control channel.)
 */
STATIC ssize_t fusd_process_write(struct file *file,
   const char *user_msg_buffer, size_t user_msg_len,
   const char *user_data_buffer, size_t user_data_len)
{
  fusd_dev_t *fusd_dev;
  fusd_msg_t *msg = NULL;
  int retval = 0;
  int yield = 0;

  GET_FUSD_DEV(file->private_data, fusd_dev);
  LOCK_FUSD_DEV(fusd_dev);

  /* get the header from userspace (first make sure there's enough data) */
  if (user_msg_len != sizeof(fusd_msg_t)) {
    RDEBUG(6, "control channel got bad write of %d bytes (wanted %d)",
	   (int) user_msg_len, (int) sizeof(fusd_msg_t));
    retval = -EINVAL;
    goto out_no_free;
  }
  if ((msg = KMALLOC(sizeof(fusd_msg_t), GFP_KERNEL)) == NULL) {
    retval = -ENOMEM;
    RDEBUG(1, "yikes!  kernel can't allocate memory");
    goto out;
  }
  memset(msg, 0, sizeof(fusd_msg_t));

  if (copy_from_user(msg, user_msg_buffer, sizeof(fusd_msg_t))) {
    retval = -EFAULT;
    goto out;
  }
  msg->data = NULL; /* pointers from userspace have no meaning */

  /* check the magic number before acting on the message at all */
  if (msg->magic != FUSD_MSG_MAGIC) {
    RDEBUG(2, "got invalid magic number on /dev/fusd write from pid %d",
	   current->pid);
    retval = -EIO;
    goto out;
  }

  /* now get data portion of the message */
  if (user_data_len < 0 || user_data_len > MAX_RW_SIZE) {
    RDEBUG(2, "fusd_process_write: got invalid length %d", (int) user_data_len);
    retval = -EINVAL;
    goto out;
  }
  if (msg->datalen != user_data_len) {
    RDEBUG(2, "msg->datalen(%d) != user_data_len(%d), sigh!",
	   msg->datalen, (int) user_data_len);
    retval = -EINVAL;
    goto out;
  }
  if (user_data_len > 0) {
    if (user_data_buffer == NULL) {
      RDEBUG(2, "msg->datalen and no data buffer, sigh!");
      retval = -EINVAL;
      goto out;
    }
    if ((msg->data = VMALLOC(user_data_len)) == NULL) {
      retval = -ENOMEM;
      RDEBUG(1, "yikes!  kernel can't allocate memory");
      goto out;
    }
    if (copy_from_user(msg->data, user_data_buffer, user_data_len)) {
      retval = -EFAULT;
      goto out;
    }
  }

  /* before device registration, the only command allowed is 'register'. */
  /*
  if (!fusd_dev->handle && msg->cmd != FUSD_REGISTER_DEVICE) {
    RDEBUG(2, "got a message other than 'register' on a new device!");
    retval = -EINVAL;
    goto out;
  }
  */

  /* now dispatch the command to the appropriate handler */
  switch (msg->cmd) {
  case FUSD_REGISTER_DEVICE:
    retval = fusd_register_device(fusd_dev, msg->parm.register_msg);
    goto out;
    break;
  case FUSD_FOPS_REPLY:
    /* if reply is successful, DO NOT free the message */
    if ((retval = fusd_fops_reply(fusd_dev, msg)) == 0) {
      yield = 1;
      goto out_no_free;
    }
    break;
  case FUSD_FOPS_NONBLOCK_REPLY:
    switch (msg->subcmd) {
    case FUSD_POLL_DIFF:
      retval = fusd_polldiff_reply(fusd_dev, msg);
      break;
    default:
      RDEBUG(2, "fusd_fops_nonblock got unknown subcmd %d", msg->subcmd);
      retval = -EINVAL;
    }
    break;
  default:
    RDEBUG(2, "warning: unknown message type of %d received!", msg->cmd);
    retval = -EINVAL;
    goto out;
    break;
  }


 out:
  if (msg && msg->data) {
    VFREE(msg->data);
    msg->data = NULL;
  }
  if (msg != NULL) {
    KFREE(msg);
    msg = NULL;
  }

 out_no_free:

  /* the functions we call indicate success by returning 0.  we
   * convert that into a success indication by changing the retval to
   * the length of the write. */
  if (retval == 0)
    retval = user_data_len + user_msg_len;

  UNLOCK_FUSD_DEV(fusd_dev);

  /* if we successfully completed someone's syscall, yield the
   * processor to them immediately as a throughput optimization.  we
   * also hope that in the case of bulk data transfer, their next
   * syscall will come in before we are scheduled again. */
  if (yield) {
#ifdef SCHED_YIELD
    current->policy |= SCHED_YIELD;
#endif
    schedule();
  }

  return retval;

 zombie_dev:
 invalid_dev:
  RDEBUG(1, "fusd_process_write: got invalid device!");
  return -EPIPE;
}


STATIC ssize_t fusd_write(struct file *file,
    const char *buffer,
    size_t length,
    loff_t *offset)
{
  return fusd_process_write(file, buffer, length, NULL, 0);
}


STATIC ssize_t fusd_writev(struct file *file,
			   const struct iovec *iov,
			   unsigned long count,
			   loff_t *offset)
{
  if (count != 2) {
    RDEBUG(2, "fusd_writev: got illegal iov count of %ld", count);
    return -EINVAL;
  }

  return fusd_process_write(file,
			    iov[0].iov_base, iov[0].iov_len,
			    iov[1].iov_base, iov[1].iov_len);
}


/* fusd_read: a process is reading on /dev/fusd. return any messages
 * waiting to go from kernel to userspace.
 *
 * Important note: there are 2 possible read modes;
 *   1) header-read mode; just the fusd_msg structure is returned.
 *
 *   2) data-read mode; the data portion of a call (NOT including the
 *   fusd_msg structure) is returned.
 *
 * The protocol this function expects the user-space library to follow
 * is:
 *   1) Userspace library reads header.
 *   2) If fusd_msg->datalen == 0, goto step 4.
 *   3) Userspace library reads data.
 *   4) Message gets dequeued by the kernel.
 *
 * In other words, userspace first reads the header.  Then, if and
 * only if the header you read indicates that data follows, userspace
 * follows with a read for that data.
 *
 * For the header read, the length requested MUST be the exact length
 * sizeof(fusd_msg_t).  The corresponding data read must request
 * exactly the number of bytes in the data portion of the message.  NO
 * OTHER READ LENGTHS ARE ALLOWED - ALL OTHER READ LENGTHS WILL GET AN
 * -EINVAL.  This is done as a basic safety measure to make sure we're
 * talking to a userspace library that understands our protocol, and
 * to detect framing errors.
 *
 * (note: normally you'd have to worry about reentrancy in a function
 * like this because the process can block on the userspace access and
 * another might try to read.  usually we would copy the message into
 * a temp location to make sure two processes don't get the same
 * message.  however in this very specialized case, we're okay,
 * because each instance of /dev/fusd has a completely independent
 * message queue.)  */


/* do a "header" read: used by fusd_read */
STATIC int fusd_read_header(char *user_buffer, size_t user_length, fusd_msg_t *msg)
{
  int len = sizeof(fusd_msg_t);

  if (user_length != len) {
    RDEBUG(4, "bad length of %d sent to /dev/fusd for peek", (int) user_length);
    return -EINVAL;
  }

  if (copy_to_user(user_buffer, msg, len))
    return -EFAULT;

  return sizeof(fusd_msg_t);
}


/* do a "data" read: used by fusd_read */
STATIC int fusd_read_data(char *user_buffer, size_t user_length, fusd_msg_t *msg)
{
  int len = msg->datalen;

  if (len == 0 || msg->data == NULL) {
    RDEBUG(1, "fusd_read_data: no data to send!");
    return -EIO;
  }

  /* make sure the user is requesting exactly the right amount (as a
     sanity check) */
  if (user_length != len) {
    RDEBUG(4, "bad read for %d bytes on /dev/fusd (need %d)", (int) user_length,len);
    return -EINVAL;
  }

  /* now copy to userspace */
  if (copy_to_user(user_buffer, msg->data, len))
    return -EFAULT;

  /* done! */
  return len;
}


STATIC ssize_t fusd_read(struct file *file,
    char *user_buffer,    /* The buffer to fill with data */
    size_t user_length,   /* The length of the buffer */
    loff_t *offset)  /* Our offset in the file */
{
  fusd_dev_t *fusd_dev;
  fusd_msgC_t *msg_out;
  int retval, dequeue = 0;

  GET_FUSD_DEV(file->private_data, fusd_dev);
  LOCK_FUSD_DEV(fusd_dev);

  RDEBUG(15, "driver pid %d (/dev/%s) entering fusd_read", current->pid,
	 NAME(fusd_dev));

  /* if no messages are waiting, either block or return EAGAIN */
  while ((msg_out = fusd_dev->msg_head) == NULL) {
    DECLARE_WAITQUEUE(wait, current);

    if (file->f_flags & O_NONBLOCK) {
      retval = -EAGAIN;
      goto out;
    }

    /*
     * sleep, waiting for a message to arrive.  we are unrolling
     * interruptible_sleep_on to avoid a race between unlocking the
     * device and sleeping (what if a message arrives in that
     * interval?)
     */
    current->state = TASK_INTERRUPTIBLE;
    add_wait_queue(&fusd_dev->dev_wait, &wait);
    UNLOCK_FUSD_DEV(fusd_dev);
    schedule();
    remove_wait_queue(&fusd_dev->dev_wait, &wait);
    LOCK_FUSD_DEV(fusd_dev);

    /* we're back awake!  --see if a signal woke us up */
    if (signal_pending(current)) {
      retval = -ERESTARTSYS;
      goto out;
    }
  }

  /* is this a header read or data read? */
  if (!msg_out->peeked) {
    /* this is a header read (first read) */
    retval = fusd_read_header(user_buffer, user_length, &msg_out->fusd_msg);

    /* is there data?  if so, make sure next read gets data.  if not,
     * make sure message is dequeued now.*/
    if (msg_out->fusd_msg.datalen) {
      msg_out->peeked = 1;
      dequeue = 0;
    } else {
      dequeue = 1;
    }
  } else {
    /* this is a data read (second read) */
    retval = fusd_read_data(user_buffer, user_length, &msg_out->fusd_msg);
    dequeue = 1; /* message should be dequeued */
  }

  /* if this message is done, take it out of the outgoing queue */
  if (dequeue) {
    if (fusd_dev->msg_tail == fusd_dev->msg_head)
      fusd_dev->msg_tail = fusd_dev->msg_head = NULL;
    else
      fusd_dev->msg_head = msg_out->next;
    FREE_FUSD_MSGC(msg_out);
  }

 out:
  UNLOCK_FUSD_DEV(fusd_dev);
  return retval;

 zombie_dev:
 invalid_dev:
  RDEBUG(2, "got read on /dev/fusd for unknown device!");
  return -EPIPE;
}


/* a poll on /dev/fusd itself (the control channel) */
STATIC unsigned int fusd_poll(struct file *file, poll_table *wait)
{
  fusd_dev_t *fusd_dev;
  GET_FUSD_DEV(file->private_data, fusd_dev);

  poll_wait(file, &fusd_dev->dev_wait, wait);

  if (fusd_dev->msg_head != NULL) {
    return POLLIN | POLLRDNORM;
  }

 invalid_dev:
  return 0;
}


STATIC struct file_operations fusd_fops = {
  owner:    THIS_MODULE,
  open:     fusd_open,
  read:     fusd_read,
  write:    fusd_write,
  writev:   fusd_writev,
  release:  fusd_release,
  poll:     fusd_poll,
};
  


/*************************************************************************/

typedef struct fusd_status_state {
  int binary_status;
  int need_new_status;
  char *curr_status;
  int curr_status_len;
  int last_version_seen;
} fusd_statcontext_t;

/* open() called on /dev/fusd/status */
STATIC int fusd_status_open(struct inode *inode, struct file *file)
{
  int error = 0;
  fusd_statcontext_t *fs;

  //MOD_INC_USE_COUNT;

  if ((fs = KMALLOC(sizeof(fusd_statcontext_t), GFP_KERNEL)) == NULL) {
    RDEBUG(1, "yikes!  kernel can't allocate memory");
    error = -ENOMEM;
    goto out;
  }

  memset(fs, 0, sizeof(fusd_statcontext_t));
  fs->need_new_status = 1;
  file->private_data = (void *) fs;

 out:
  //if (error)
  //  MOD_DEC_USE_COUNT;
  return error;
}

/* close on /dev/fusd_status */
STATIC int fusd_status_release(struct inode *inode, struct file *file)
{
  fusd_statcontext_t *fs = (fusd_statcontext_t *) file->private_data;

  if (fs) {
    if (fs->curr_status)
      KFREE(fs->curr_status);
    KFREE(fs);
  }

  //MOD_DEC_USE_COUNT;
  return 0;
}


/* ioctl() on /dev/fusd/status */
STATIC int fusd_status_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, unsigned long arg)
{
  fusd_statcontext_t *fs = (fusd_statcontext_t *) file->private_data;

  if (!fs)
    return -EIO;

  switch (cmd) {
  case FUSD_STATUS_USE_BINARY:
    fs->binary_status = 1;
    return 0;
  default:
    return -EINVAL;
    break;
  }
}


/*
 * maybe_expand_buffer: expand a buffer exponentially as it fills.  We
 * are given:
 *
 * - A reference to a pointer to a buffer (buf)
 * - A reference to the buffer's current capacity (buf_size)
 * - The current amount of buffer space used (len)
 * - The amount of space we want to ensure is free in the buffer (space_needed)
 *
 * If there isn't at least space_needed difference between buf_size
 * and len, the existing contents are moved into a larger buffer. 
 */
STATIC int maybe_expand_buffer(char **buf, int *buf_size, int len,
			       int space_needed)
{
  if (*buf_size - len < space_needed) {
    char *old_buf = *buf;

    *buf_size *= 2;
    *buf = KMALLOC(*buf_size, GFP_KERNEL);

    if (*buf != NULL)
      memmove(*buf, old_buf, len);
    KFREE(old_buf);
    if (*buf == NULL) {
      RDEBUG(1, "out of memory!");
      return -1;
    }
  }
  return 0;
}



/* Build a text buffer containing current fusd status. */
STATIC void fusd_status_build_text(fusd_statcontext_t *fs)
{
  int buf_size = 512;
  char *buf = KMALLOC(buf_size, GFP_KERNEL);
  int len = 0, total_clients = 0, total_files = 0;
  struct list_head *tmp;

  if (buf == NULL) {
    RDEBUG(1, "fusd_status_build: out of memory!");
    return;
  }

  len += snprintf(buf + len, buf_size - len,
		  "  PID  Open Name\n"
		  "------ ---- -----------------\n");

  down(&fusd_devlist_sem);
  list_for_each(tmp, &fusd_devlist_head) {
    fusd_dev_t *d = list_entry(tmp, fusd_dev_t, devlist);

    if (!d)
      continue;

    /* Possibly expand the buffer if we need more space */
    if (maybe_expand_buffer(&buf, &buf_size, len, FUSD_MAX_NAME_LENGTH+120) < 0)
      goto out;

    len += snprintf(buf + len, buf_size - len,
		    "%6d %4d %s%s\n", d->pid, d->num_files,
		    d->zombie ? "<zombie>" : "", NAME(d));

    total_files++;
    total_clients += d->num_files;
  }

  len += snprintf(buf + len, buf_size - len,
		  "\nFUSD $Revision$ - %d devices used by %d clients\n",
		  total_files, total_clients);

 out:
  fs->last_version_seen = last_version;
  up(&fusd_devlist_sem);

  if (fs->curr_status)
    KFREE(fs->curr_status);

  fs->curr_status = buf;
  fs->curr_status_len = len;
  fs->need_new_status = 0;
}


/* Build the binary version of status */
STATIC void fusd_status_build_binary(fusd_statcontext_t *fs)
{
  int buf_size = 512;
  char *buf = KMALLOC(buf_size, GFP_KERNEL);
  int len = 0, i = 0;
  struct list_head *tmp;
  fusd_status_t *s;

  if (buf == NULL) {
    RDEBUG(1, "out of memory!");
    return;
  }

  down(&fusd_devlist_sem);
  list_for_each(tmp, &fusd_devlist_head) {
    fusd_dev_t *d = list_entry(tmp, fusd_dev_t, devlist);

    if (!d)
      continue;

    /* Possibly expand the buffer if we need more space */
    if (maybe_expand_buffer(&buf, &buf_size, len, sizeof(fusd_status_t)) < 0)
      goto out;

    s = &((fusd_status_t *) buf)[i];

    /* construct this status entry */
    memset(s, 0, sizeof(fusd_status_t));
    strncpy(s->name, NAME(d), FUSD_MAX_NAME_LENGTH);
    s->zombie   = d->zombie;
    s->pid      = d->pid;
    s->num_open = d->num_files;

    i++;
    len += sizeof(fusd_status_t);
  }
  
 out:
  fs->last_version_seen = last_version;
  up(&fusd_devlist_sem);

  if (fs->curr_status)
    KFREE(fs->curr_status);

  fs->curr_status = buf;
  fs->curr_status_len = len;
  fs->need_new_status = 0;
}



STATIC ssize_t fusd_status_read(struct file *file,
    char *user_buffer,    /* The buffer to fill with data */
    size_t user_length,   /* The length of the buffer */
    loff_t *offset)  /* Our offset in the file */
{
  fusd_statcontext_t *fs = (fusd_statcontext_t *) file->private_data;

  if (!fs)
    return -EIO;

  /* create a new status page, if we aren't in the middle of one */
  if (fs->need_new_status) {
    if (fs->binary_status)
      fusd_status_build_binary(fs);
    else
      fusd_status_build_text(fs);
  }

  /* return EOF if we're at the end */
  if (fs->curr_status == NULL || fs->curr_status_len == 0) {
    fs->need_new_status = 1;
    return 0;
  }

  /* return only as much data as we have */
  if (fs->curr_status_len < user_length)
    user_length = fs->curr_status_len;
  if (copy_to_user(user_buffer, fs->curr_status, user_length))
    return -EFAULT;

  /* update fs, so we don't return the same data next time */
  fs->curr_status_len -= user_length;
  if (fs->curr_status_len)
    memmove(fs->curr_status, fs->curr_status + user_length, fs->curr_status_len);
  else {
    KFREE(fs->curr_status);
    fs->curr_status = NULL;
  }

  return user_length;
}


/* a poll on /dev/fusd itself (the control channel) */
STATIC unsigned int fusd_status_poll(struct file *file, poll_table *wait)
{
  fusd_statcontext_t *fs = (fusd_statcontext_t *) file->private_data;

  poll_wait(file, &new_device_wait, wait);

  if (fs->last_version_seen < last_version)
    return POLLIN | POLLRDNORM;
  else
    return 0;
}


STATIC struct file_operations fusd_status_fops = {
  owner:    THIS_MODULE,
  open:     fusd_status_open,
  ioctl:    fusd_status_ioctl,
  read:     fusd_status_read,
  release:  fusd_status_release,
  poll:     fusd_status_poll,
};
  

/*************************************************************************/


STATIC int init_fusd(void)
{
	int retval;

#ifdef CONFIG_FUSD_MEMDEBUG
  if ((retval = fusd_mem_init()) < 0)
    return retval;
#endif


  printk(KERN_INFO
	 "fusd: starting, $Revision$, $Date$");
#ifdef CVSTAG
  printk(", release %s", CVSTAG);
#endif
#ifdef CONFIG_FUSD_DEBUG
  printk(", debuglevel=%d\n", fusd_debug_level);
#else
  printk(", debugging messages disabled\n");
#endif

	fusd_control_device = NULL;
	fusd_status_device = NULL;
	
	fusd_class = class_create(THIS_MODULE, "fusd");
	if(IS_ERR(fusd_class))
	{
		retval = PTR_ERR(fusd_class);
		printk(KERN_ERR "class_create failed status: %d\n", retval);
		goto fail0;
	}
	
	control_id = 0;

	if((retval = alloc_chrdev_region(&control_id, 0, 1, FUSD_CONTROL_FILENAME)) < 0)
	{
		printk(KERN_ERR "alloc_chrdev_region failed status: %d\n", retval);
		goto fail1;
	}

	fusd_control_device = cdev_alloc();
	if(fusd_control_device == NULL)
	{
		printk(KERN_ERR "cdev-alloc failed\n");
		retval = -ENOMEM;
		goto fail3;
	}

	fusd_control_device->owner = THIS_MODULE;
	fusd_control_device->ops = &fusd_fops;
	kobject_set_name(&fusd_control_device->kobj, FUSD_CONTROL_FILENAME);

	printk(KERN_ERR "cdev control id: %d\n", control_id);
	if((retval = cdev_add(fusd_control_device, control_id, 1)) < 0)
	{
		printk(KERN_ERR "cdev_add failed status: %d\n", retval);
		kobject_put(&fusd_control_device->kobj);
		goto fail4;
	}
	
	fusd_control_class_device = CLASS_DEVICE_CREATE(fusd_class, NULL, control_id, NULL, "control");
	if(fusd_control_class_device == NULL)
	{
		retval = PTR_ERR(fusd_control_class_device);
		printk("class_device_create failed status: %d\n", retval);
		goto fail5;
	}

	status_id = 0;

	if((retval = alloc_chrdev_region(&status_id, 0, 1, FUSD_STATUS_FILENAME)) < 0)
	{
		printk(KERN_ERR "alloc_chrdev_region failed status: %d\n", retval);
		goto fail6;
	}

	fusd_status_device = cdev_alloc();
	if(fusd_status_device == NULL)
	{
		retval = -ENOMEM;
		goto fail8;
	}

	fusd_status_device->owner = THIS_MODULE;
	fusd_status_device->ops = &fusd_status_fops;
	kobject_set_name(&fusd_status_device->kobj, FUSD_STATUS_FILENAME);

	if((retval = cdev_add(fusd_status_device, status_id, 1)) < 0)
	{
		printk(KERN_ERR "cdev_add failed status: %d\n", retval);
		kobject_put(&fusd_status_device->kobj);
		goto fail9;
	}
	
	fusd_status_class_device = CLASS_DEVICE_CREATE(fusd_class, NULL, status_id, NULL, "status");
	if(fusd_status_class_device == NULL)
	{
		printk(KERN_ERR "class_device_create failed status: %d\n", retval);
		retval = PTR_ERR(fusd_status_class_device);
		goto fail10;
	}
	
  RDEBUG(1, "registration successful");
  return 0;

fail10:
	cdev_del(fusd_status_device);
fail9:
	kfree(fusd_status_device);
fail8:

	//fail7:
	unregister_chrdev_region(status_id, 1);
fail6:
	class_device_destroy(fusd_class, control_id);
fail5:
	cdev_del(fusd_control_device);
fail4:
	kfree(fusd_control_device);
fail3:

	//fail2:
	unregister_chrdev_region(control_id, 1);

fail1:
	class_destroy(fusd_class);
fail0:
	return retval;
}

STATIC void cleanup_fusd(void)
{
  RDEBUG(1, "cleaning up");

	class_device_destroy(fusd_class, status_id);
	class_device_destroy(fusd_class, control_id);
	
	cdev_del(fusd_control_device);
	cdev_del(fusd_status_device);

	class_destroy(fusd_class);
	
#ifdef CONFIG_FUSD_MEMDEBUG
  fusd_mem_cleanup();
#endif
}

module_init(init_fusd);
module_exit(cleanup_fusd);

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
 * ioctl.c: Shows both the client side and server side of FUSD ioctl
 * servicing.
 *
 * There's a lot of extra cruft in this example program (compared to
 * the other examples, anyway), because this program is both an
 * example and part of the regression test suite.
 *
 * $Id: ioctl.c,v 1.4 2003/07/11 22:29:39 cerpa Exp $ 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "fusd.h"

/* EXAMPLE START ioctl.h */
/* definition of the structure exchanged between client and server */
struct ioctl_data_t {
  char string1[60];
  char string2[60];
};

#define IOCTL_APP_TYPE 71 /* arbitrary number unique to this app */

#define IOCTL_TEST0 _IO(IOCTL_APP_TYPE, 0) /* no argument */  /* SKIPLINE */
#define IOCTL_TEST1 _IO(IOCTL_APP_TYPE, 1) /* int argument */ /* SKIPLINE */
#define IOCTL_TEST2 _IO(IOCTL_APP_TYPE, 2) /* int argument */
#define IOCTL_TEST3 _IOR(IOCTL_APP_TYPE, 3, struct ioctl_data_t)
#define IOCTL_TEST4 _IOW(IOCTL_APP_TYPE, 4, struct ioctl_data_t)
#define IOCTL_TEST5 _IOWR(IOCTL_APP_TYPE, 5, struct ioctl_data_t)
/* EXAMPLE STOP ioctl.h */
#define IOCTL_TEST_TERMINATE _IO(IOCTL_APP_TYPE, 6)

#define TEST1_NUM 12345
#define TEST3_STRING1 "This is test3 - string1"
#define TEST3_STRING2 "This is test3 - string2"
#define TEST4_STRING1 "This is test 4's string1"
#define TEST4_STRING2 "This is test 4's string2"
#define TEST5_STRING1_IN  "If you're happy and you know it"
#define TEST5_STRING2_IN  "clap your hands!"
#define TEST5_STRING1_OUT "IF YOU'RE HAPPY AND YOU KNOW IT"
#define TEST5_STRING2_OUT "CLAP YOUR HANDS!"


#define CHECK(condition) do { \
  if (!(condition)) { \
      printf("%s: TEST FAILED\n", __STRING(condition)); \
      errors++; \
  } \
} while(0)


int zeroreturn(struct fusd_file_info *file) { return 0; }

/* EXAMPLE START ioctl-server.c */
/* This function is run by the driver */
int do_ioctl(struct fusd_file_info *file, int cmd, void *arg)
{
  static int errors = 0;	/* SKIPLINE */
  char *c;			/* SKIPLINE */
  struct ioctl_data_t *d;

  if (_IOC_TYPE(cmd) != IOCTL_APP_TYPE)
    return 0;

  switch (cmd) {
/* EXAMPLE STOP ioctl-server.c */
  case IOCTL_TEST0:
    printf("ioctl server: got test0, returning 0\n");
    return 0;
    break;

  case IOCTL_TEST1:
  case IOCTL_TEST2:
    printf("ioctl server: got test1/2, arg=%d, returning it\n", (int) arg);
    return (int) arg;
    break;

/* EXAMPLE START ioctl-server.c */
  case IOCTL_TEST3: /* returns data to the client */
    d = arg;
    printf("ioctl server: got test3 request (read-only)\n");/* SKIPLINE */
    printf("ioctl server: ...returning test strings for client to read\n"); /* SKIPLINE */
    strcpy(d->string1, TEST3_STRING1);
    strcpy(d->string2, TEST3_STRING2);
    return 0;
    break;

  case IOCTL_TEST4: /* gets data from the client */
    d = arg;
    printf("ioctl server: got test4 request (write-only)\n"); /* SKIPLINE */
    printf("ioctl server: ...got the following strings written by client:\n"); /* SKIPLINE */
    printf("ioctl server: test4, string1: got '%s'\n", d->string1);
    printf("ioctl server: test4, string2: got '%s'\n", d->string2);
    CHECK(!strcmp(d->string1, TEST4_STRING1));/* SKIPLINE */
    CHECK(!strcmp(d->string2, TEST4_STRING2)); /* SKIPLINE */
    return 0;
    break;
/* EXAMPLE STOP ioctl-server.c */

  case IOCTL_TEST5:
    d = arg;
    printf("ioctl server: got test5 request (read+write)\n");
    printf("ioctl server: test5, string1: got '%s'\n", d->string1);
    printf("ioctl server: test5, string2: got '%s'\n", d->string2);
    printf("ioctl server: capitalizing the strings and returning them\n");
    for (c = d->string1; *c; c++)
      *c = toupper(*c);
    for (c = d->string2; *c; c++)
      *c = toupper(*c);
    return 0;
    break;

  case IOCTL_TEST_TERMINATE:
    printf("ioctl server: got request to terminate, calling exit(%d)\n",
	   errors);
    printf("ioctl server: note: client should see -EPIPE\n");
    exit(errors);
    break;

/* EXAMPLE START ioctl-server.c */
  default:
    printf("ioctl server: got unknown cmd, sigh, this is broken\n");
    return -EINVAL;
    break;
  }

  return 0;
}
/* EXAMPLE STOP ioctl-server.c */

int main(int argc, char *argv[])
{
  pid_t server_pid, retpid;

  if ((server_pid = fork()) < 0) {
    perror("error creating server");
    exit(1);
  }

  if (server_pid == 0) {
    /* ioctl server */
    struct fusd_file_operations f = { open: zeroreturn, close: zeroreturn,
				      ioctl: do_ioctl};
    if (fusd_register("ioctltest", 0666, NULL, &f) < 0)
      perror("registering ioctltest");
    printf("server starting\n");
    fusd_run();
  } else {
    /* ioctl client */
/* EXAMPLE START ioctl-client.c */
    int fd, ret;
    struct ioctl_data_t d;
/* EXAMPLE STOP ioctl-client.c */
    int errors, status;

    errors = 0;

    sleep(1);
/* EXAMPLE START ioctl-client.c */

    if ((fd = open("/dev/ioctltest", O_RDWR)) < 0) {
      perror("client: can't open ioctltest");
      exit(1);
    }

/* EXAMPLE STOP ioctl-client.c */
    errors = 0;

    /* test0: simply issue a command and get a retval */
    ret = ioctl(fd, IOCTL_TEST0);
    printf("ioctl test0: got %d (expecting 0)\n\n", ret);
    CHECK(ret == 0);

    /* test1: issue a command with a simple (integer) argument */
    ret = ioctl(fd, IOCTL_TEST1, TEST1_NUM);
    CHECK(ret == TEST1_NUM);
    CHECK(errno == 0);
    printf("ioctl test1: got %d, errno=%d (expecting %d, errno=0)\n\n",
	   ret, errno, TEST1_NUM);

    /* test2 again: make sure errno is set properly */
    ret = ioctl(fd, IOCTL_TEST2, -ELIBBAD);
    CHECK(errno == ELIBBAD);
    CHECK(ret == -1);
    printf("ioctl test2: got %d, errno=%d (expecting -1, errno=%d)\n\n",
	   ret, errno, ELIBBAD);

    printf("ioctl test3: expecting retval 0, string This Is Test3\n");
/* EXAMPLE START ioctl-client.c */
    /* test3: make sure we can get data FROM a driver using ioctl */
    ret = ioctl(fd, IOCTL_TEST3, &d);
    CHECK(ret == 0);		/* SKIPLINE */
    CHECK(!strcmp(d.string1, TEST3_STRING1)); /* SKIPLINE */
    CHECK(!strcmp(d.string2, TEST3_STRING2)); /* SKIPLINE */
    printf("ioctl test3: got retval=%d\n", ret);
    printf("ioctl test3: got string1='%s'\n", d.string1);
    printf("ioctl test3: got string2='%s'\n", d.string2);
    printf("\n");		/* SKIPLINE */

    /* test4: make sure we can send data TO a driver using an ioctl */
    printf("ioctl test4: server should see string 'This Is Test4'\n");/* SKIPLINE */
    sprintf(d.string1, TEST4_STRING1);
    sprintf(d.string2, TEST4_STRING2);
    ret = ioctl(fd, IOCTL_TEST4, &d);
/* EXAMPLE STOP ioctl-client.c */
    CHECK(ret == 0);
    printf("\n");

    /* test5: we send 2 strings to the ioctl server, they should come
     * back in all caps */
    printf("ioctl test5: we send strings that should come back capitalized\n");
    sprintf(d.string1, TEST5_STRING1_IN);
    sprintf(d.string2, TEST5_STRING2_IN);
    printf("ioctl test5: sending  string1='%s'\n", d.string1);
    printf("ioctl test5: sending  string2='%s'\n", d.string2);
    ret = ioctl(fd, IOCTL_TEST5, &d);
    CHECK(ret == 0);
    CHECK(!strcmp(d.string1, TEST5_STRING1_OUT));
    CHECK(!strcmp(d.string2, TEST5_STRING2_OUT));
    printf("ioctl test5: got retval=%d\n", ret);
    printf("ioctl test5: got back string1='%s'\n", d.string1);
    printf("ioctl test5: got back string2='%s'\n", d.string2);
    printf("\n");

    /* now tell the server to terminate, we should get EPIPE */
    ret = ioctl(fd, IOCTL_TEST_TERMINATE);
    CHECK(errno == EPIPE);
    CHECK(ret == -1);
    printf("ioctl termination test: got %d (errno=%d)\n", ret, errno);
    printf("ioctl termination tets: expecting ret=-1, errno=%d\n\n", EPIPE);

    printf("ioctl client: waiting for server to terminate...\n");
    retpid = wait(&status);
    CHECK(retpid == server_pid);
    CHECK(WEXITSTATUS(status) == 0);
      
    printf("ioctl test done - %d errors\n", errors);
    if (errors) {
      printf("IOCTL REGRESSION TEST FAILED\n");
      exit(1);
    } else {
      printf("all tests passed\n");
      exit(0);
    }
  }

  return 0;
}

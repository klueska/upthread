/* See COPYING.LESSER for copyright information. */
/* Kevin Klues <klueska@cs.berkeley.edu>	*/

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <upthread/upthread.h>

const char filename[] = "emergency.dat";
const char str[] = "This is a test of the emergency broadcast system.  This is only a test";

void *thread_func(void *arg)
{
  int fd = open(filename, O_RDONLY);
  assert(fd != -1);

  char buf[sizeof(str)];
  assert(read(fd, buf, sizeof(buf)-1) == sizeof(buf)-1);
  buf[sizeof(buf)-1] = 0;
  assert(memcmp(buf, str, sizeof(buf)) == 0);

  assert(close(fd) == 0);
  return NULL;
}

int main (int argc, char **argv)
{
  upthread_t handle;
  upthread_attr_t attr;

  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  int n = write(fd, str, strlen(str));
  assert(n == strlen(str));

  printf("main upthread_self: %p\n", upthread_self());

  upthread_attr_init(&attr);
  upthread_attr_setstacksize(&attr, 32768);
  upthread_create(&handle, &attr, &thread_func, NULL);
  upthread_join(handle, NULL);

  close(fd);

  return 0;
}

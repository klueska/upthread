#include <upthread.h>

/* Attr stuff. */
int upthread_attr_init(upthread_attr_t *a)
{
	a->stacksize = UPTHREAD_STACK_SIZE;
	a->detachstate = UPTHREAD_CREATE_JOINABLE;
	a->stackaddr = NULL;
	return 0;
}

int upthread_attr_destroy(upthread_attr_t *a)
{
	return 0;
}

int upthread_attr_setstacksize(upthread_attr_t *attr, size_t stacksize)
{
	attr->stacksize = stacksize;
	return 0;
}
int upthread_attr_getstacksize(const upthread_attr_t *attr, size_t *stacksize)
{
	*stacksize = attr->stacksize;
	return 0;
}

int upthread_attr_setdetachstate(upthread_attr_t *__attr, int __detachstate)
{
	__attr->detachstate = __detachstate;
	return 0;
}

upthread_t upthread_self()
{
	return (upthread_t)current_uthread;
}

/* Compare two thread identifiers.  */
int upthread_equal(upthread_t __thread1, upthread_t __thread2)
{
	return __thread1 == __thread2;
}

/* Return the previously set address for the stack.  */
int upthread_attr_getstack(const upthread_attr_t *__restrict __attr,
                           void **__stackaddr, size_t *__stacksize)
{
	*__stackaddr = __attr->stackaddr;
	*__stacksize = __attr->stacksize;
	return 0;
}


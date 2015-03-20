#include <upthread.h>

/* Compare two thread identifiers.  */
int upthread_equal(upthread_t __thread1, upthread_t __thread2)
{
	return __thread1 == __thread2;
}

/* Initialize thread attribute *ATTR with attributes corresponding to the
   already running thread TH.  It shall be called on uninitialized ATTR
   and destroyed with pthread_attr_destroy when no longer needed.  */
int upthread_getattr_np (upthread_t __th, upthread_attr_t *__attr)
{
	__attr->stackaddr = __th->stacktop - __th->stacksize;
	__attr->stacksize = __th->stacksize;
	if (__th->detached)
		__attr->detachstate = UPTHREAD_CREATE_DETACHED;
	else
		__attr->detachstate = UPTHREAD_CREATE_JOINABLE;
	return 0;
}

/* Return the previously set address for the stack.  */
int upthread_attr_getstack(const upthread_attr_t *__restrict __attr,
                           void **__stackaddr, size_t *__stacksize)
{
	*__stackaddr = __attr->stackaddr;
	*__stacksize = __attr->stacksize;
	return 0;
}


#include "signal.h"
#include "upthread.h"

int pthread_mutex_timedlock (pthread_mutex_t *__restrict __mutex,
				    const struct timespec *__restrict
				    __abstime)
{
	fprintf(stderr, "Unsupported %s!\n", __FUNCTION__);
	abort();
	return -1;
}

int pthread_cond_timedwait (pthread_cond_t *__restrict __cond,
				   pthread_mutex_t *__restrict __mutex,
				   const struct timespec *__restrict __abstime)
{
	fprintf(stderr, "Unsupported %s!\n", __FUNCTION__);
	abort();
	return -1;
}

int upthread_once (upthread_once_t *__once_control,
                   void (*__init_routine) (void))
{
	fprintf(stderr, "Unsupported %s!\n", __FUNCTION__);
	abort();
	return -1;
}

int upthread_cancel (pthread_t __th)
{
	fprintf(stderr, "Unsupported %s!\n", __FUNCTION__);
	abort();
	return -1;
}

int upthread_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
	return pthread_sigmask(how, set, oldset);
	fprintf(stderr, "Unsupported %s!\n", __FUNCTION__);
	abort();
	return -1;
}


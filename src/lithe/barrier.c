#include "upthread.h"

int upthread_barrierattr_init(upthread_barrierattr_t *attr)
{
	return 0;
}
int upthread_barrierattr_destroy(upthread_barrierattr_t *attr)
{
	return 0;
}

int upthread_barrier_init(upthread_barrier_t* barrier,
                          const upthread_barrierattr_t* a, int n)
{
	lithe_barrier_init(barrier, n);
	return 0;
}

int upthread_barrier_destroy(upthread_barrier_t* barrier)
{
	lithe_barrier_destroy(barrier);
	return 0;
}

int upthread_barrier_wait(upthread_barrier_t* barrier)
{
	lithe_barrier_wait(barrier);
	return 0;
}



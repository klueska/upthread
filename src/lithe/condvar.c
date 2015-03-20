#include "upthread.h"

int upthread_condattr_init(upthread_condattr_t *a)
{
	return 0;
}

int upthread_condattr_destroy(upthread_condattr_t *a)
{
	return 0;
}

int upthread_cond_init(upthread_cond_t *c, const upthread_condattr_t *a)
{
	return lithe_condvar_init(c);
}

int upthread_cond_destroy(upthread_cond_t *c)
{
	return 0;
}

int upthread_cond_wait(upthread_cond_t *c, upthread_mutex_t *m)
{
	return lithe_condvar_wait(c, m);
}

int upthread_cond_signal(upthread_cond_t *c)
{
	return lithe_condvar_signal(c);
}

int upthread_cond_broadcast(upthread_cond_t *c)
{
	return lithe_condvar_broadcast(c);
}


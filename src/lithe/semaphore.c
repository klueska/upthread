#include "upthread.h"

int upthread_sem_init(upthread_sem_t *sem, int count)
{
	return lithe_sem_init(sem, count);
}

int upthread_sem_wait(upthread_sem_t *sem)
{
	return lithe_sem_wait(sem);
}

int upthread_sem_post(upthread_sem_t *sem)
{
	return lithe_sem_post(sem);
}


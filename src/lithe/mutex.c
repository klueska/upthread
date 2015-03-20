#include "internal/assert.h"
#include "upthread.h"

int upthread_mutexattr_init(upthread_mutexattr_t* attr)
{
	return lithe_mutexattr_init(attr);
}

int upthread_mutexattr_destroy(upthread_mutexattr_t* attr)
{
	return 0;
}

int upthread_mutexattr_gettype(const upthread_mutexattr_t* attr, int* type)
{
	assert((int)UPTHREAD_MUTEX_NORMAL == (int)LITHE_MUTEX_NORMAL);
	assert((int)UPTHREAD_MUTEX_RECURSIVE == (int)LITHE_MUTEX_RECURSIVE);
	return lithe_mutexattr_gettype((upthread_mutexattr_t*)attr, type);
}

int upthread_mutexattr_settype(upthread_mutexattr_t* attr, int type)
{
	assert((int)UPTHREAD_MUTEX_NORMAL == (int)LITHE_MUTEX_NORMAL);
	assert((int)UPTHREAD_MUTEX_RECURSIVE == (int)LITHE_MUTEX_RECURSIVE);
	return lithe_mutexattr_settype(attr, type);
}

int upthread_mutex_init(upthread_mutex_t* mutex, 
                        const upthread_mutexattr_t* attr)
{
	return lithe_mutex_init(mutex, (upthread_mutexattr_t*)attr);
}

int upthread_mutex_trylock(upthread_mutex_t* mutex)
{
	return lithe_mutex_trylock(mutex);
}

int upthread_mutex_lock(upthread_mutex_t* mutex)
{
	return lithe_mutex_lock(mutex);
}

int upthread_mutex_unlock(upthread_mutex_t* mutex)
{
	return lithe_mutex_unlock(mutex);
}

int upthread_mutex_destroy(upthread_mutex_t* mutex)
{
	return 0;
}


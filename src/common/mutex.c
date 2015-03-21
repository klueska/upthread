#include <errno.h>
#include <sys/queue.h>
#include "internal/assert.h"
#include "upthread.h"

int upthread_mutexattr_init(upthread_mutexattr_t* attr)
{
	if(attr == NULL)
		return EINVAL;
	attr->type = UPTHREAD_MUTEX_DEFAULT;
	return 0;
}

int upthread_mutexattr_destroy(upthread_mutexattr_t* attr)
{
	return 0;
}

int upthread_mutexattr_gettype(const upthread_mutexattr_t* attr, int* type)
{
	if(attr == NULL)
		return EINVAL;
	*type = attr->type;
	return 0;
}

int upthread_mutexattr_settype(upthread_mutexattr_t* attr, int type)
{
	if(attr == NULL)
		return EINVAL;
	if(type >= NUM_UPTHREAD_MUTEX_TYPES)
		return EINVAL;
	attr->type = type;
	return 0;
}

int upthread_mutex_init(upthread_mutex_t* mutex, 
                        const upthread_mutexattr_t* attr)
{
	if(mutex == NULL)
		return EINVAL;
	if(attr == NULL)
		upthread_mutexattr_init(&mutex->attr);
	else
		mutex->attr = *attr;

	/* Do initialization. */
	STAILQ_INIT(&mutex->queue);
	spin_pdr_init(&mutex->lock);
	//mutex->qnode = NULL;
	mutex->locked = 0;
	mutex->owner = NULL;
	return 0;
}

static void block(struct uthread *uthread, void *arg)
{
	upthread_t upthread = (upthread_t)uthread;
	upthread_mutex_t *mutex = (upthread_mutex_t *) arg;

	assert(mutex);
	uthread_has_blocked(uthread, UTH_EXT_BLK_MUTEX);
	STAILQ_INSERT_TAIL(&mutex->queue, upthread, next);
	spin_pdr_unlock(&mutex->lock);
}

int upthread_mutex_trylock(upthread_mutex_t* mutex)
{
	if(mutex == NULL)
		return EINVAL;

	int retval = 0;
	spin_pdr_lock(&mutex->lock);
	if(mutex->attr.type == UPTHREAD_MUTEX_RECURSIVE &&
		mutex->owner == upthread_self()) {
		mutex->locked++;
	}
	else if(mutex->locked) {
		retval = EBUSY;
	}
	else {
		mutex->owner = upthread_self();
		mutex->locked++;
	}
	spin_pdr_unlock(&mutex->lock);
	return retval;
}

int upthread_mutex_lock(upthread_mutex_t* mutex)
{
	if(mutex == NULL)
		return EINVAL;

	spin_pdr_lock(&mutex->lock);
	if(mutex->attr.type == UPTHREAD_MUTEX_RECURSIVE &&
		mutex->owner == upthread_self()) {
		mutex->locked++;
	}
	else {
		while(mutex->locked) {
			uthread_yield(true, block, mutex);

			spin_pdr_lock(&mutex->lock);
		}
		mutex->owner = upthread_self();
		mutex->locked++;
	}
	spin_pdr_unlock(&mutex->lock);
	return 0;
}

int upthread_mutex_unlock(upthread_mutex_t* mutex)
{
	if(mutex == NULL)
		return EINVAL;

	spin_pdr_lock(&mutex->lock);
	mutex->locked--;
	if(mutex->locked == 0) {
		upthread_t upthread = STAILQ_FIRST(&mutex->queue);
		if(upthread)
			STAILQ_REMOVE_HEAD(&mutex->queue, next);
		mutex->owner = NULL;
		spin_pdr_unlock(&mutex->lock);

		if(upthread != NULL) {
			uthread_runnable((struct uthread*)upthread);
		}
	}
	else {
		spin_pdr_unlock(&mutex->lock);
	}
	return 0;
}

int upthread_mutex_destroy(upthread_mutex_t* mutex)
{
	return 0;
}


#include <errno.h>
#include <sys/queue.h>
#include <assert.h>
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
	mcs_lock_init(&mutex->lock);
	mutex->qnode = NULL;
	mutex->locked = 0;
	mutex->owner = NULL;
	return 0;
}

static void block(struct uthread *uthread, void *arg)
{
	upthread_t upthread = (upthread_t)uthread;
	upthread_mutex_t *mutex = (upthread_mutex_t *) arg;

	assert(mutex);
	__upthread_generic_yield(upthread);
	upthread->state = UPTH_BLK_MUTEX;
	STAILQ_INSERT_TAIL(&mutex->queue, upthread, next);
	mcs_lock_unlock(&mutex->lock, mutex->qnode);
}

int upthread_mutex_trylock(upthread_mutex_t* mutex)
{
	if(mutex == NULL)
		return EINVAL;

	int retval = 0;
	mcs_lock_qnode_t qnode = {0};
	mcs_lock_lock(&mutex->lock, &qnode);
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
	mcs_lock_unlock(&mutex->lock, &qnode);
	return retval;
}

int upthread_mutex_lock(upthread_mutex_t* mutex)
{
	if(mutex == NULL)
		return EINVAL;

	mcs_lock_qnode_t qnode = {0};
	mcs_lock_lock(&mutex->lock, &qnode);
	if(mutex->attr.type == UPTHREAD_MUTEX_RECURSIVE &&
		mutex->owner == upthread_self()) {
		mutex->locked++;
	}
	else {
		while(mutex->locked) {
			mutex->qnode = &qnode;
			uthread_yield(true, block, mutex);

			memset(&qnode, 0, sizeof(mcs_lock_qnode_t));
			mcs_lock_lock(&mutex->lock, &qnode);
		}
		mutex->owner = upthread_self();
		mutex->locked++;
	}
	mcs_lock_unlock(&mutex->lock, &qnode);
	return 0;
}

int upthread_mutex_unlock(upthread_mutex_t* mutex)
{
	if(mutex == NULL)
		return EINVAL;

	mcs_lock_qnode_t qnode = {0};
	mcs_lock_lock(&mutex->lock, &qnode);
	mutex->locked--;
	if(mutex->locked == 0) {
		upthread_t upthread = STAILQ_FIRST(&mutex->queue);
		if(upthread)
			STAILQ_REMOVE_HEAD(&mutex->queue, next);
		mutex->owner = NULL;
		mcs_lock_unlock(&mutex->lock, &qnode);

		if(upthread != NULL) {
			uthread_runnable((struct uthread*)upthread);
		}
	}
	else {
		mcs_lock_unlock(&mutex->lock, &qnode);
	}
	return 0;
}

int upthread_mutex_destroy(upthread_mutex_t* mutex)
{
	return 0;
}


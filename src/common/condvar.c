#include <errno.h>
#include <sys/queue.h>
#include "internal/assert.h"
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
	if(c == NULL)
		return EINVAL;

	mcs_lock_init(&c->lock);
	STAILQ_INIT(&c->queue);
	return 0;
}

int upthread_cond_destroy(upthread_cond_t *c)
{
	return 0;
}

static void block(struct uthread *uthread, void *arg)
{
	upthread_t upthread = (upthread_t)uthread;
	upthread_cond_t *condvar = (upthread_cond_t *) arg;

	assert(condvar);
	uthread_has_blocked(uthread, UTH_EXT_BLK_MUTEX);
	STAILQ_INSERT_TAIL(&condvar->queue, upthread, next);
	upthread_mutex_unlock(condvar->waiting_mutex);
	mcs_lock_unlock(&condvar->lock, condvar->waiting_qnode);
}

int upthread_cond_wait(upthread_cond_t *c, upthread_mutex_t *m)
{
	if(c == NULL)
		return EINVAL;
	if(m == NULL)
		return EINVAL;

	mcs_lock_qnode_t qnode = {0};
	mcs_lock_lock(&c->lock, &qnode);
	c->waiting_mutex = m;
	c->waiting_qnode = &qnode;
	uthread_yield(true, block, c);
	return upthread_mutex_lock(m);
}

int upthread_cond_signal(upthread_cond_t *c)
{
	if(c == NULL)
		return EINVAL;

	mcs_lock_qnode_t qnode = {0};
	mcs_lock_lock(&c->lock, &qnode);
	upthread_t upthread = STAILQ_FIRST(&c->queue);
	if(upthread)
		STAILQ_REMOVE_HEAD(&c->queue, next);
	mcs_lock_unlock(&c->lock, &qnode);

	if (upthread != NULL) {
		uthread_runnable((struct uthread*)upthread);
	}
	return 0;
}

int upthread_cond_broadcast(upthread_cond_t *c)
{
	if(c == NULL)
		return EINVAL;

	mcs_lock_qnode_t qnode = {0};
	while(1) {
		mcs_lock_lock(&c->lock, &qnode);
		upthread_t upthread = STAILQ_FIRST(&c->queue);
		if(upthread)
			STAILQ_REMOVE_HEAD(&c->queue, next);
		else break;
		mcs_lock_unlock(&c->lock, &qnode);
		uthread_runnable((struct uthread*)upthread);
		memset(&qnode, 0, sizeof(mcs_lock_qnode_t));
	}
	mcs_lock_unlock(&c->lock, &qnode);
	return 0;
}


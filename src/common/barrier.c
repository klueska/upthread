#include <errno.h>
#include <sys/queue.h>
#include "internal/assert.h"
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
	assert(barrier != NULL);
	barrier->N = n;
	barrier->arrived = 0;
	barrier->wait = false;
	barrier->signals = (padded_bool_t *)calloc(n, sizeof(padded_bool_t));
	barrier->blocked[0].queue = (upthread_t *)malloc(n * sizeof(upthread_t));
	barrier->blocked[1].queue = (upthread_t *)malloc(n * sizeof(upthread_t));
	barrier->blocked[0].len = 0;
	barrier->blocked[1].len = 0;
	barrier->blocked[0].maxlen = n;
	barrier->blocked[1].maxlen = n;
	mcs_lock_init(&barrier->blocked[0].mtx);
	mcs_lock_init(&barrier->blocked[1].mtx);
	return 0;
}

int upthread_barrier_wait(upthread_barrier_t* barrier)
{
	assert(barrier != NULL);
	free(barrier->signals);
	free(barrier->blocked[0].queue);
	free(barrier->blocked[1].queue);
	return 0;
}

static void __barrier_block(struct uthread *uthread, void *__blocked)
{
	upthread_t upthread = (upthread_t)uthread;
	contextq_t *blocked = (contextq_t *)__blocked;

	assert(blocked != NULL);
	assert(blocked->len < blocked->maxlen);
	__upthread_generic_yield(upthread);
	upthread->state = UPTH_BLK_MUTEX;
	blocked->queue[blocked->len] = upthread;
	blocked->len += 1;
	mcs_lock_unlock(&blocked->mtx, blocked->qnode);
}
int upthread_barrier_destroy(upthread_barrier_t* barrier)
{
	assert(barrier != NULL);

	/* toggled signal value for barrier reuse */
	bool wait = barrier->wait;

	/* increment the counter to signal arrival */
	int id = __sync_fetch_and_add(&barrier->arrived, 1);

	/* if last to arrive at barrier, release everyone else */ 
	if (id == (barrier->N - 1)) {
		/* reset barrier */
		barrier->arrived = 0;
		rmb();
		barrier->wait = !barrier->wait;
		wmb();

		/* signal everyone that they can continue */
		int i;
		for (i = 0; i < barrier->N; i++) {
			barrier->signals[i].val = !wait;
		}

		/* unblock those that are no longer running */
		contextq_t *blocked = &barrier->blocked[wait];
		mcs_lock_qnode_t qnode = {0};
		mcs_lock_lock(&blocked->mtx, &qnode);
		for (i = 0; i < blocked->len; i++) {
			uthread_runnable((struct uthread*)blocked->queue[i]);
		}
		blocked->len = 0;
		mcs_lock_unlock(&blocked->mtx, &qnode);
	} 
	
	/* wait for remaining to arrive */
	else {
		/* spin for MAXSTALLS to wait for minor load imbalance */
		/* release hart afterwards to avoid deadlock */
		const int MAXSTALLS = 1000;
		int nstalls = 0;
		while (barrier->signals[id].val == wait) { 
			nstalls++;
			if (nstalls >= MAXSTALLS) {
				contextq_t *blocked = &barrier->blocked[wait];
				mcs_lock_qnode_t qnode = {0};
				mcs_lock_lock(&blocked->mtx, &qnode);
				if (barrier->signals[id].val == wait) {
					blocked->qnode = &qnode;
					uthread_yield(true, __barrier_block, (void *)blocked);
				} else {
					mcs_lock_unlock(&blocked->mtx, &qnode);
				}
			}
			cpu_relax();
		}
	}
	return 0;
}



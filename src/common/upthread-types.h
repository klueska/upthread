#ifndef UPTHREAD_TYPES_H
#define UPTHREAD_TYPES_H

#include <sys/queue.h>
#include <parlib/uthread.h>
#include <parlib/dtls.h>
#include <parlib/mcs.h>
#include <parlib/spinlock.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pthread struct.  First has to be the uthread struct, which the vcore code
 * will access directly (as if upthread_tcb is a struct uthread). */
struct upthread_tcb;
struct upthread_tcb {
	struct uthread uthread;
	STAILQ_ENTRY(upthread_tcb) next;
	int state;
	bool detached;
	struct upthread_tcb *joiner;			/* raced on by exit and join */
	uint32_t id;
	int stack_offset;
	uint32_t stacksize;
	void *stacktop;
	void *(*start_routine)(void*);
	void *arg;
	void *retval;
	int preferred_vcq;
};
typedef struct upthread_tcb* upthread_t;
STAILQ_HEAD(upthread_queue, upthread_tcb);

/* The core upthreads API */
typedef struct {
	void *stackaddr;
	size_t stacksize;
	int detachstate;
} upthread_attr_t;

/* Upthread mutexes */
typedef struct upthread_mutexattr {
	int type;
} upthread_mutexattr_t;

typedef struct upthread_mutex {
	upthread_mutexattr_t attr;
	struct upthread_queue queue;
	spin_pdr_lock_t lock;
	int locked;
	upthread_t owner;
} upthread_mutex_t;
#define UPTHREAD_MUTEX_INITIALIZER(mutex) { \
	.attr = {0}, \
	.queue = STAILQ_HEAD_INITIALIZER((mutex).queue), \
	.lock = SPINPDR_INITIALIZER, \
	.locked = 0, \
	.owner = NULL \
}

/* Upthread condvars */
typedef struct upthread_condvar {
	mcs_pdr_lock_t lock;
	mcs_lock_qnode_t *waiting_qnode;
	upthread_mutex_t *waiting_mutex;
	struct upthread_queue queue;
} upthread_cond_t;
#define UPTHREAD_CONDVAR_INITIALIZER(condvar) { \
	.lock = MCS_PDRLOCK_INIT, \
	.waiting_qnode = NULL, \
	.waiting_mutex = NULL, \
	.queue = STAILQ_HEAD_INITIALIZER((condvar).queue) \
}
typedef void upthread_condattr_t;

/* Upthread barriers */
typedef union {
	bool val;
	uint8_t padding[ARCH_CL_SIZE];
} padded_bool_t;

typedef struct {
	upthread_t *queue;
	int len;
	mcs_pdr_lock_t mtx;
	mcs_lock_qnode_t *qnode;
	int maxlen;
} contextq_t;

typedef struct upthread_barrier {
	int N;
	int arrived;
	bool wait;
	padded_bool_t *signals;
	contextq_t blocked[2];
} upthread_barrier_t;
typedef void upthread_barrierattr_t;

/* Sempahores */
typedef struct upthread_sem {
  int value;
  int nwaiters;
} upthread_sem_t;
#define UPTHREAD_SEM_INITIALIZER {0, 0}

/* Get/Setpsecific stuff */
typedef dtls_key_t* upthread_key_t;

/* Unsupported stuff */
typedef void *upthread_once_t;

#ifdef __cplusplus
}
#endif

#endif // UPTHREAD_COMMON_H

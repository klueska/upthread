#ifndef UPTHREAD_TYPES_H
#define UPTHREAD_TYPES_H

#include <lithe/context.h>
#include <lithe/barrier.h>
#include <lithe/mutex.h>
#include <lithe/condvar.h>
#include <lithe/semaphore.h>
#include <parlib/dtls.h>

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
	uint32_t stacksize;
	void *stacktop;
	void *(*start_routine)(void*);
	void *arg;
	void *retval;
	int preferred_vcq;
} __attribute__((aligned(ARCH_CL_SIZE)));
typedef struct upthread_tcb* upthread_t;
STAILQ_HEAD(upthread_queue, upthread_tcb);
//struct upthread_tcb {
//	struct lithe_context ctx;
//} __attribute__((aligned(ARCH_CL_SIZE)));
//typedef struct upthread_tcb* upthread_t;

/* The core upthreads API */
typedef struct {
	void *stackaddr;
	size_t stacksize;
	int detachstate;
} upthread_attr_t;

/* Upthread mutexes */
typedef lithe_mutex_t upthread_mutex_t;
#define UPTHREAD_MUTEX_INITIALIZER LITHE_MUTEX_INITIALIZER
typedef lithe_mutexattr_t upthread_mutexattr_t;

/* Upthread condvars */
typedef lithe_condvar_t upthread_cond_t;
#define UPTHREAD_CONDVAR_INITIALIZER LITHE_CONDVAR_INITIALIZER
typedef void upthread_condattr_t;

/* Upthread barriers */
typedef lithe_barrier_t upthread_barrier_t;
typedef void upthread_barrierattr_t;

/* Semaphores */
typedef lithe_sem_t upthread_sem_t;
#define UPTHREAD_SEM_INITIALIZER LITHE_SEM_INITIALIZER

/* Get/Setpsecific stuff */
typedef dtls_key_t* upthread_key_t;

/* Unsupported stuff */
typedef void *upthread_once_t;

#ifdef __cplusplus
}
#endif


#endif // UPTHREAD_TYPES_H

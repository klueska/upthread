#ifndef UPTHREAD_TYPES_H
#define UPTHREAD_TYPES_H

#include <lithe/fork_join_sched.h>
#include <lithe/barrier.h>
#include <lithe/mutex.h>
#include <lithe/condvar.h>
#include <lithe/semaphore.h>
#include <parlib/dtls.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pthread struct. */
struct upthread_lithe_context;
struct upthread_lithe_context {
	lithe_fork_join_context_t context;
	bool detached;
	struct upthread_lithe_context *joiner;
	void *(*start_routine)(void*);
	void *arg;
	void *retval;
};
typedef struct upthread_lithe_context upthread_lithe_context_t;
typedef struct upthread_lithe_context* upthread_t;

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

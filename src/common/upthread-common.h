#ifndef UPTHREAD_COMMMON_H
#define UPTHREAD_COMMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/* Stack stuff. */
#define UPTHREAD_STACK_PAGES 1024
#define UPTHREAD_STACK_SIZE (UPTHREAD_STACK_PAGES*PGSIZE)

/* Pthread states.  These are mostly examples for other 2LSs */
#define UPTH_CREATED		1
#define UPTH_RUNNABLE		2
#define UPTH_RUNNING		3
#define UPTH_EXITING		4
#define UPTH_BLK_YIELDING	5	/* brief state btw pth_yield and pth_runnable */
#define UPTH_BLK_JOINING	6	/* joining on a child */
#define UPTH_BLK_SYSC		7	/* blocked on a syscall */
#define UPTH_BLK_MUTEX		8	/* blocked externally, possibly on a mutex */
#define UPTH_BLK_PAUSED		9	/* handed back to us from uthread code */

/* The core upthreads API */
enum {
  UPTHREAD_CREATE_JOINABLE,
#define UPTHREAD_CREATE_JOINABLE	UPTHREAD_CREATE_JOINABLE
  UPTHREAD_CREATE_DETACHED
#define UPTHREAD_CREATE_DETACHED	UPTHREAD_CREATE_DETACHED
};

int upthread_attr_init(upthread_attr_t *);
int upthread_attr_destroy(upthread_attr_t *);
int upthread_attr_setdetachstate(upthread_attr_t *__attr, int __detachstate);
int upthread_attr_getstacksize(const upthread_attr_t *attr, size_t *stacksize);
int upthread_attr_setstacksize(upthread_attr_t *attr, size_t stacksize);
int upthread_create(upthread_t *, const upthread_attr_t *,
                   void *(*)(void *), void *);
int upthread_join(upthread_t, void **);
int upthread_yield(void) __THROW;
void upthread_exit(void* ret);
int upthread_detach(upthread_t __th);
upthread_t upthread_self();
int upthread_tid();

/* Upthread mutexes */
enum {
	UPTHREAD_MUTEX_NORMAL,
	UPTHREAD_MUTEX_RECURSIVE,
	NUM_UPTHREAD_MUTEX_TYPES,
};
#define UPTHREAD_MUTEX_DEFAULT UPTHREAD_MUTEX_NORMAL

int upthread_mutexattr_init(upthread_mutexattr_t *);
int upthread_mutexattr_destroy(upthread_mutexattr_t *);
int upthread_mutexattr_gettype(const upthread_mutexattr_t *, int *);
int upthread_mutexattr_settype(upthread_mutexattr_t *, int);
int upthread_mutex_init(upthread_mutex_t *, const upthread_mutexattr_t *);
int upthread_mutex_trylock(upthread_mutex_t *);
int upthread_mutex_lock(upthread_mutex_t *);
int upthread_mutex_unlock(upthread_mutex_t *);
int upthread_mutex_destroy(upthread_mutex_t *);

/* Upthread condvars */
int upthread_cond_init(upthread_cond_t *, const upthread_condattr_t *);
int upthread_cond_destroy(upthread_cond_t *);
int upthread_cond_broadcast(upthread_cond_t *);
int upthread_cond_signal(upthread_cond_t *);
int upthread_cond_wait(upthread_cond_t *, upthread_mutex_t *);

/* Upthread barriers */
int upthread_barrierattr_init(upthread_barrierattr_t *attr);
int upthread_barrierattr_destroy(upthread_barrierattr_t *attr);
int upthread_barrier_init(upthread_barrier_t* b,
                          const upthread_barrierattr_t* a, int count);
int upthread_barrier_wait(upthread_barrier_t* b);
int upthread_barrier_destroy(upthread_barrier_t* b);

/* Get/Setpsecific stuff */
int upthread_key_create (upthread_key_t *__key,
                        void (*__destr_function) (void *));
int upthread_key_delete (upthread_key_t __key);
void *upthread_getspecific (upthread_key_t __key);
int upthread_setspecific (upthread_key_t __key, const void *__pointer);

/* Common stuff. */
int upthread_equal(upthread_t __thread1, upthread_t __thread2);
int upthread_getattr_np(upthread_t __th, upthread_attr_t *__attr);
int upthread_attr_getstack(const upthread_attr_t *__attr,
                           void **__stackaddr, size_t *__stacksize);

/* Unsupported Stuff */
extern int upthread_mutex_timedlock (upthread_mutex_t *__restrict __mutex,
				    const struct timespec *__restrict
				    __abstime) __THROWNL __nonnull ((1, 2));
extern int upthread_cond_timedwait (upthread_cond_t *__restrict __cond,
				   upthread_mutex_t *__restrict __mutex,
				   const struct timespec *__restrict __abstime)
     __nonnull ((1, 2, 3));
extern int upthread_once (upthread_once_t *__once_control,
			 void (*__init_routine) (void)) __nonnull ((1, 2));
extern int upthread_cancel (pthread_t __th);

#ifdef __cplusplus
}
#endif

#endif // UPTHREAD_COMMON_H

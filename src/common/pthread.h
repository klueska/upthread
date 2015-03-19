#ifndef UPTHREAD_PTHREAD_H
#define UPTHREAD_PTHREAD_H

#include <upthread/upthread.h>

/* Detach state. */
#define PTHREAD_CREATE_JOINABLE UPTHREAD_CREATE_JOINABLE
#define PTHREAD_CREATE_DETACHED UPTHREAD_CREATE_DETACHED

/* Attr stuff. (Not all attrs supported yet) */
#define pthread_attr_t              upthread_attr_t
#define pthread_attr_init           upthread_attr_init
#define pthread_attr_destroy        upthread_attr_destroy
#define pthread_attr_setdetachstate upthread_attr_setdetachstate
#define pthread_attr_getstacksize   upthread_attr_getstacksize
#define pthread_attr_setstacksize   upthread_attr_setstacksize

/* Common API funcs. (Not complete) */
#define pthread_t      upthread_t
#define pthread_create upthread_create
#define pthread_join   upthread_join
#define pthread_yield  upthread_yield
#define pthread_exit   upthread_exit
#define pthread_detach upthread_detach
#define pthread_self   upthread_self

/* Mutex stuff. (Not all types supported) */
#define PTHREAD_MUTEX_NORMAL    UPTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_RECURSIVE UPTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_DEFAULT   UPTHREAD_MUTEX_DEFAULT

#define pthread_mutexattr_t       upthread_mutexattr_t
#define pthread_mutex_t           upthread_mutex_t
#define pthread_mutexattr_init    upthread_mutexattr_init
#define pthread_mutexattr_destroy upthread_mutexattr_destroy
#define pthread_mutexattr_gettype upthread_mutexattr_gettype
#define pthread_mutexattr_settype upthread_mutexattr_settype
#define pthread_mutex_init        upthread_mutex_init
#define pthread_mutex_trylock     upthread_mutex_trylock
#define pthread_mutex_lock        upthread_mutex_lock
#define pthread_mutex_unlock      upthread_mutex_unlock
#define pthread_mutex_destroy     upthread_mutex_destroy

/* Condvar stuff. (Lots of stuff not supported yet) */
#define pthread_cond_t         upthread_cond_t
#define pthread_condattr_t     upthread_condattr_t
#define pthread_cond_init      upthread_cond_init
#define pthread_cond_destroy   upthread_cond_destroy
#define pthread_cond_broadcast upthread_cond_broadcast
#define pthread_cond_signal    upthread_cond_signal
#define pthread_cond_wait      upthread_cond_wait

/* Barrier stuff. (Lots of stuff not supported yet) */
#define pthread_barrierattr_t       upthread_barrierattr_t
#define pthread_barrierattr_init    upthread_barrierattr_init
#define pthread_barrierattr_destroy upthread_barrierattr_destroy
#define pthread_barrier_init        upthread_barrier_init
#define pthread_barrier_wait        upthread_barrier_wait
#define pthread_barrier_destroy     upthread_barrier_destroy

/* Semaphore stuff. (Some stuff not supported yet) */
#define upthread_sem_t    sem_t
#define upthread_sem_init sem_init
#define upthread_sem_wait sem_wait
#define upthread_sem_post sem_post

/* Get/Setspecific stuff. */
#define pthread_key_t       upthread_key_t
#define pthread_key_create  upthread_key_create
#define pthread_key_delete  upthread_key_delete
#define pthread_getspecific upthread_getspecific
#define pthread_setspecific upthread_setspecific

#endif // UPTHREAD_PTHREAD_H

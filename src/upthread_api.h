/*
 * Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifndef _BTHREAD_API_H
#define _BTHREAD_API_H

int upthread_attr_init(upthread_attr_t *);
int upthread_attr_destroy(upthread_attr_t *);
int upthread_create(upthread_t *, const upthread_attr_t *,
                   void *(*)(void *), void *);
int upthread_join(upthread_t, void **);
int upthread_yield(void);

int upthread_attr_setdetachstate(upthread_attr_t *__attr,int __detachstate);

int upthread_mutex_destroy(upthread_mutex_t *);
int upthread_mutex_init(upthread_mutex_t *, const upthread_mutexattr_t *);
int upthread_mutex_lock(upthread_mutex_t *);
int upthread_mutex_trylock(upthread_mutex_t *);
int upthread_mutex_unlock(upthread_mutex_t *);
int upthread_mutex_destroy(upthread_mutex_t *);

int upthread_mutexattr_init(upthread_mutexattr_t *);
int upthread_mutexattr_destroy(upthread_mutexattr_t *);
int upthread_mutexattr_gettype(const upthread_mutexattr_t *, int *);
int upthread_mutexattr_settype(upthread_mutexattr_t *, int);

int upthread_cond_init(upthread_cond_t *, const upthread_condattr_t *);
int upthread_cond_destroy(upthread_cond_t *);
int upthread_cond_broadcast(upthread_cond_t *);
int upthread_cond_signal(upthread_cond_t *);
int upthread_cond_wait(upthread_cond_t *, upthread_mutex_t *);

int upthread_condattr_init(upthread_condattr_t *);
int upthread_condattr_destroy(upthread_condattr_t *);
int upthread_condattr_setpshared(upthread_condattr_t *, int);
int upthread_condattr_getpshared(upthread_condattr_t *, int *);

int upthread_rwlock_destroy(upthread_mutex_t *);
int upthread_rwlock_init(upthread_mutex_t *, const upthread_mutexattr_t *);
int upthread_rwlock_unlock(upthread_mutex_t *);
int upthread_rwlock_rdlock(upthread_mutex_t *);
int upthread_rwlock_wrlock(upthread_mutex_t *);
int upthread_rwlock_tryrdlock(upthread_mutex_t *);
int upthread_rwlock_trywrlock(upthread_mutex_t *);

upthread_t upthread_self();
int upthread_equal(upthread_t t1, upthread_t t2);
void upthread_exit(void* ret);
int upthread_once(upthread_once_t* once_control, void (*init_routine)(void));

int upthread_barrier_init(upthread_barrier_t* b, const upthread_barrierattr_t* a, int count);
int upthread_barrier_wait(upthread_barrier_t* b);
int upthread_barrier_destroy(upthread_barrier_t* b);

int upthread_detach(upthread_t __th);
int upthread_attr_setstacksize(upthread_attr_t *attr, size_t stacksize);
int upthread_attr_getstacksize(const upthread_attr_t *attr, size_t *stacksize);

#endif

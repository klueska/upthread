/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See COPYING for details.
 */

/**
 * Interface of upthread semaphores
 */

#ifndef UPTHREAD_SEMAPHORE_H
#define UPTHREAD_SEMAPHORE_H

#include "upthread.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize a semaphore. */
int upthread_sem_init(upthread_sem_t *sem, int pshared, int count);

/* Destroy a semaphore. */
int upthread_sem_destroy(upthread_sem_t *sem);

/* Wait on a semaphore. */
int upthread_sem_wait(upthread_sem_t *sem);

/* Post on a semaphore. */
int upthread_sem_post(upthread_sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif // UPTHREAD_SEMAPHORE_H

/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See COPYING for details.
 */

/**
 * Interface of upthread semaphores
 */

#ifndef UPTHREAD_SEMAPHORE_H
#define UPTHREAD_SEMAPHORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* A upthread semaphore struct */
typedef struct upthread_sem {
  int value;
  int nwaiters;
} upthread_sem_t;
#define UPTHREAD_SEM_INITIALIZER {0, 0}

/* Initialize a semaphore. */
int upthread_sem_init(upthread_sem_t *sem, int count);

/* Wait on a semaphore. */
int upthread_sem_wait(upthread_sem_t *sem);

/* Post on a semaphore. */
int upthread_sem_post(upthread_sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif // UPTHREAD_SEMAPHORE_H
